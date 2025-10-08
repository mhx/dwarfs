/* vim:set ts=2 sw=2 sts=2 et: */
/**
 * \author     Marcus Holland-Moritz (github@mhxnet.de)
 * \copyright  Copyright (c) Marcus Holland-Moritz
 *
 * This file is part of dwarfs.
 *
 * dwarfs is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * dwarfs is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with dwarfs.  If not, see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <cassert>
#include <cstdint>

#ifdef _WIN32
#include <folly/portability/Windows.h>
#include <winioctl.h>
#else
#include <cerrno>

#include <fcntl.h>
#include <unistd.h>
#endif

#include <dwarfs/binary_literals.h>
#include <dwarfs/detail/file_extent_info.h>

#include <dwarfs/utility/internal/file_writer.h>

#include "sparse_file_builder.h"

namespace dwarfs::test {

using namespace std::string_literals;
using namespace dwarfs::binary_literals;

namespace {

using dwarfs::utility::internal::diagnostic_sink;
using dwarfs::utility::internal::file_writer;

void throw_if_error(std::error_code const& ec, char const* what) {
  if (ec) {
    throw std::system_error(ec, what);
  }
}

class null_diagnostic_sink : public diagnostic_sink {
 public:
  void warning(std::filesystem::path const&, std::string_view,
               std::optional<std::error_code>) override {}
};

} // namespace

class sparse_file_builder::impl {
 public:
  static std::unique_ptr<impl>
  create_temporary(std::filesystem::path const& dir, diagnostic_sink& ds,
                   std::error_code& ec) {
    return create(dir, true, ds, ec);
  }

  static std::unique_ptr<impl>
  create(std::filesystem::path const& path, diagnostic_sink& ds,
         std::error_code& ec) {
    return create(path, false, ds, ec);
  }

  explicit impl(file_writer&& fw)
      : fw_{std::move(fw)} {}

  ~impl() {
    std::error_code ec;
    commit(ec);
  }

  void truncate(file_size_t size, std::error_code& ec) {
    fw_.truncate(size, ec);
  }

  void
  write_data(file_off_t offset, std::string_view data, std::error_code& ec) {
    fw_.write_data(offset, data.data(), data.size(), ec);
  }

  void punch_hole(file_off_t off, file_off_t len, std::error_code& ec) {
    fw_.write_hole(off, len, ec);
  }

  void commit(std::error_code& ec) { fw_.commit(ec); }

  size_t get_first_data_offset(std::error_code& ec) {
#ifdef _WIN32
    auto const h = std::any_cast<HANDLE>(fw_.get_native_handle());

    assert(h != INVALID_HANDLE_VALUE);

    LARGE_INTEGER size_li{};
    if (!GetFileSizeEx(h, &size_li)) {
      ec = std::error_code(::GetLastError(), std::system_category());
      return 0;
    }

    FILE_ALLOCATED_RANGE_BUFFER in{};
    in.FileOffset.QuadPart = 0;
    in.Length = size_li;

    FILE_ALLOCATED_RANGE_BUFFER out[8]{};
    DWORD out_bytes = 0;

    if (!::DeviceIoControl(h, FSCTL_QUERY_ALLOCATED_RANGES, &in, sizeof(in),
                           out, sizeof(out), &out_bytes, nullptr)) {
      ec = std::error_code(::GetLastError(), std::system_category());
      return 0;
    }

    size_t const count = out_bytes / sizeof(FILE_ALLOCATED_RANGE_BUFFER);
    if (count == 0) {
      // No allocated ranges in [0, upto_exclusive), i.e. file is all hole.
      ec = std::make_error_code(std::errc::io_error);
      return 0;
    }

    return static_cast<size_t>(out[0].FileOffset.QuadPart);
#else
#if defined(SEEK_HOLE) && defined(SEEK_DATA)
    auto const fd = std::any_cast<int>(fw_.get_native_handle());

    assert(fd >= 0);

    off_t data_off = ::lseek(fd, 0, SEEK_DATA);

    if (data_off < 0) {
      ec = std::error_code(errno, std::generic_category());
      return 0;
    }

    return static_cast<size_t>(data_off);
#else
    ec = std::make_error_code(std::errc::function_not_supported);
    return 0;
#endif
#endif
  }

 private:
  static std::unique_ptr<impl>
  create(std::filesystem::path const& p, bool temp, diagnostic_sink& ds,
         std::error_code& ec) {
    auto fw = temp ? file_writer::create_native_temp(p, ds, ec)
                   : file_writer::create_native(p, ds, ec);

    if (!ec) {
      fw.set_sparse(ec);

      if (!ec) {
        return std::make_unique<impl>(std::move(fw));
      }
    }

    return nullptr;
  }

  file_writer fw_;
};

std::optional<size_t>
sparse_file_builder::hole_granularity(std::filesystem::path const& path) {
  std::error_code ec;
  null_diagnostic_sink ds;
  auto builder = impl::create_temporary(path, ds, ec);

  if (!builder) {
    return std::nullopt;
  }

  static constexpr size_t kMaxTestSize{1_MiB};

  size_t hole_size{1};

  while (hole_size <= kMaxTestSize) {
    // reset file to zero length
    builder->truncate(0, ec);
    if (ec) {
      return std::nullopt;
    }

    // write a single byte at the end of the hole
    builder->write_data(hole_size, "x", ec);
    if (ec) {
      return std::nullopt;
    }

    // seek to data
    auto data_off = builder->get_first_data_offset(ec);
    if (ec) {
      return std::nullopt;
    }

    if (data_off == hole_size) {
      break;
    }

    hole_size *= 2;
  }

  if (hole_size > kMaxTestSize) {
    return std::nullopt;
  }

  return hole_size;
}

sparse_file_builder
sparse_file_builder::create(std::filesystem::path const& path,
                            std::error_code& ec) {
  static null_diagnostic_sink ds;
  return sparse_file_builder(impl::create(path, ds, ec));
}

sparse_file_builder
sparse_file_builder::create(std::filesystem::path const& path) {
  std::error_code ec;
  auto b = create(path, ec);
  throw_if_error(ec, "sparse_file_builder::create");
  return b;
}

sparse_file_builder::sparse_file_builder(std::unique_ptr<impl> pimpl)
    : impl_{std::move(pimpl)} {}

sparse_file_builder::~sparse_file_builder() = default;

void sparse_file_builder::truncate(file_size_t size, std::error_code& ec) {
  assert(impl_);
  impl_->truncate(size, ec);
}

void sparse_file_builder::truncate(file_size_t size) {
  std::error_code ec;
  truncate(size, ec);
  throw_if_error(ec, "sparse_file_builder::truncate");
}

void sparse_file_builder::write_data(file_off_t offset, std::string_view data,
                                     std::error_code& ec) {
  assert(impl_);
  impl_->write_data(offset, data, ec);
}

void sparse_file_builder::write_data(file_off_t offset, std::string_view data) {
  std::error_code ec;
  write_data(offset, data, ec);
  throw_if_error(ec, "sparse_file_builder::write_data");
}

void sparse_file_builder::punch_hole(file_off_t offset, file_off_t size,
                                     std::error_code& ec) {
  assert(impl_);
  impl_->punch_hole(offset, size, ec);
}

void sparse_file_builder::punch_hole(file_off_t offset, file_off_t size) {
  std::error_code ec;
  punch_hole(offset, size, ec);
  throw_if_error(ec, "sparse_file_builder::punch_hole");
}

void sparse_file_builder::commit(std::error_code& ec) {
  assert(impl_);
  impl_->commit(ec);
}

void sparse_file_builder::commit() {
  std::error_code ec;
  commit(ec);
  throw_if_error(ec, "sparse_file_builder::commit");
}

} // namespace dwarfs::test
