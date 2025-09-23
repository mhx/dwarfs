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

#include "sparse_file_builder.h"

namespace dwarfs::test {

using namespace std::string_literals;
using namespace dwarfs::binary_literals;

namespace {

void throw_if_error(std::error_code const& ec, char const* what) {
  if (ec) {
    throw std::system_error(ec, what);
  }
}

} // namespace

#ifdef _WIN32

class sparse_file_builder::impl {
 public:
  static std::unique_ptr<impl>
  create_temporary(std::filesystem::path const& dir,
                   std::error_code& ec) noexcept {
    auto const name = (dir / (L"dwarfs_hole_probe_" +
                              std::to_wstring(::GetCurrentProcessId()) + L"_" +
                              std::to_wstring(::GetTickCount64()) + L".tmp"))
                          .wstring();

    HANDLE h = ::CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE,
                             FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE |
                                 FILE_FLAG_OVERLAPPED,
                             nullptr);

    if (h == INVALID_HANDLE_VALUE) {
      ec = std::error_code(::GetLastError(), std::system_category());
      return nullptr;
    }

    if (!set_sparse(h, ec)) {
      ::CloseHandle(h);
      return nullptr;
    }

    return std::make_unique<impl>(h);
  }

  static std::unique_ptr<impl>
  create(std::filesystem::path const& path, std::error_code& ec) noexcept {
    HANDLE h = ::CreateFileW(
        path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);

    if (h == INVALID_HANDLE_VALUE) {
      ec = std::error_code(::GetLastError(), std::system_category());
      return nullptr;
    }

    if (!set_sparse(h, ec)) {
      ::CloseHandle(h);
      return nullptr;
    }

    return std::make_unique<impl>(h);
  }

  explicit impl(HANDLE h)
      : h_{h} {}

  ~impl() {
    std::error_code ec;
    commit(ec);
  }

  void truncate(file_size_t size, std::error_code& ec) noexcept {
    assert(h_ != INVALID_HANDLE_VALUE);
    LARGE_INTEGER li{};
    li.QuadPart = static_cast<LONGLONG>(size);
    if (!::SetFilePointerEx(h_, li, nullptr, FILE_BEGIN)) {
      ec = std::error_code(::GetLastError(), std::system_category());
    }
    if (!::SetEndOfFile(h_)) {
      ec = std::error_code(::GetLastError(), std::system_category());
    }
  }

  void write_data(file_off_t offset, std::string_view data,
                  std::error_code& ec) noexcept {
    assert(h_ != INVALID_HANDLE_VALUE);
    auto p = reinterpret_cast<uint8_t const*>(data.data());
    auto remaining = static_cast<uint64_t>(data.size());
    auto off64 = static_cast<uint64_t>(offset);

    while (remaining > 0) {
      DWORD to_write = remaining > static_cast<std::uint64_t>(MAXDWORD)
                           ? MAXDWORD
                           : static_cast<DWORD>(remaining);

      OVERLAPPED ov{};
      ov.Offset = static_cast<DWORD>(off64 & 0xFFFFFFFFull);
      ov.OffsetHigh = static_cast<DWORD>((off64 >> 32) & 0xFFFFFFFFull);

      DWORD wrote = 0;

      if (!::WriteFile(h_, p, to_write, &wrote, &ov)) {
        auto const err = ::GetLastError();
        if (err != ERROR_IO_PENDING) {
          ec = std::error_code(err, std::system_category());
          return;
        }
        if (!::GetOverlappedResult(h_, &ov, &wrote, TRUE)) {
          ec = std::error_code(::GetLastError(), std::system_category());
          return;
        }
      }

      // if nothing was written, avoid infinite loop
      if (wrote == 0) {
        ec = std::make_error_code(std::errc::io_error);
        return;
      }

      p += wrote;
      remaining -= wrote;
      off64 += wrote;
    }
  }

  void
  punch_hole(file_off_t off, file_off_t len, std::error_code& ec) noexcept {
    assert(h_ != INVALID_HANDLE_VALUE);

    FILE_ZERO_DATA_INFORMATION info{};
    info.FileOffset.QuadPart = static_cast<LONGLONG>(off);
    info.BeyondFinalZero.QuadPart = static_cast<LONGLONG>(off + len);

    DWORD bytes = 0;

    if (!::DeviceIoControl(h_, FSCTL_SET_ZERO_DATA, &info, sizeof(info),
                           nullptr, 0, &bytes, nullptr)) {
      ec = std::error_code(static_cast<int>(::GetLastError()),
                           std::system_category());
    }
  }

  void commit(std::error_code& ec) noexcept {
    if (h_ != INVALID_HANDLE_VALUE) {
      if (!::CloseHandle(h_)) {
        ec = std::error_code(::GetLastError(), std::system_category());
        return;
      }
      h_ = INVALID_HANDLE_VALUE;
    }
  }

  size_t get_first_data_offset(std::error_code& ec) noexcept {
    assert(h_ != INVALID_HANDLE_VALUE);

    LARGE_INTEGER size_li{};
    if (!GetFileSizeEx(h_, &size_li)) {
      ec = std::error_code(::GetLastError(), std::system_category());
      return 0;
    }

    FILE_ALLOCATED_RANGE_BUFFER in{};
    in.FileOffset.QuadPart = 0;
    in.Length = size_li;

    FILE_ALLOCATED_RANGE_BUFFER out[8]{};
    DWORD out_bytes = 0;

    if (!::DeviceIoControl(h_, FSCTL_QUERY_ALLOCATED_RANGES, &in, sizeof(in),
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
  }

 private:
  static bool set_sparse(HANDLE h, std::error_code& ec) {
    DWORD bytes = 0;
    if (!::DeviceIoControl(h, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &bytes,
                           nullptr)) {
      ec = std::error_code(::GetLastError(), std::system_category());
      return false;
    }
    return true;
  }

  HANDLE h_{INVALID_HANDLE_VALUE};
};

#else

class sparse_file_builder::impl {
 public:
  static std::unique_ptr<impl>
  create_temporary(std::filesystem::path const& dir,
                   std::error_code& ec) noexcept {
    auto tmpfile = (dir / "sparse_XXXXXX\0"s).string();
    auto fd = ::mkstemp(tmpfile.data());
    if (fd < 0) {
      ec = std::error_code(errno, std::generic_category());
      return nullptr;
    }
    ::unlink(tmpfile.c_str()); // best effort cleanup
    return std::make_unique<impl>(fd);
  }

  static std::unique_ptr<impl>
  create(std::filesystem::path const& path, std::error_code& ec) noexcept {
    auto fd = ::open(path.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (fd < 0) {
      ec = std::error_code(errno, std::generic_category());
      return nullptr;
    }
    return std::make_unique<impl>(fd);
  }

  explicit impl(int fd)
      : fd_{fd} {}

  ~impl() {
    std::error_code ec;
    commit(ec);
  }

  void truncate(file_size_t size, std::error_code& ec) noexcept {
    assert(fd_ >= 0);
    if (::ftruncate(fd_, static_cast<off_t>(size)) != 0) {
      ec = std::error_code(errno, std::generic_category());
    }
  }

  void write_data(file_off_t offset, std::string_view data,
                  std::error_code& ec) noexcept {
    assert(fd_ >= 0);
    auto p = data.data();
    auto remaining = data.size();
    auto off = static_cast<off_t>(offset);
    while (remaining > 0) {
      auto n = ::pwrite(fd_, p, remaining, off);
      if (n < 0) {
        if (errno == EINTR) {
          continue;
        }
        ec = std::error_code(errno, std::generic_category());
        return;
      }
      off += static_cast<off_t>(n);
      p += static_cast<size_t>(n);
      remaining -= static_cast<size_t>(n);
    }
  }

#ifdef F_PUNCHHOLE
  void
  punch_hole(file_off_t off, file_off_t len, std::error_code& ec) noexcept {
    assert(fd_ >= 0);
    if (len > 0) {
      fpunchhole_t ph{};

      ph.fp_flags = 0; // currently unused
      ph.reserved = 0;
      ph.fp_offset = off; // start of region to deallocate
      ph.fp_length = len; // size of region

      if (::fcntl(fd_, F_PUNCHHOLE, &ph) == -1) {
        ec = std::error_code(errno, std::generic_category());
      }
    }
  }
#else
  void punch_hole(file_off_t, file_off_t, std::error_code&) noexcept {}
#endif

  void commit(std::error_code& ec) noexcept {
    if (fd_ >= 0) {
      if (::close(fd_) != 0) {
        ec = std::error_code(errno, std::generic_category());
        return;
      }
      fd_ = -1;
    }
  }

  size_t get_first_data_offset(std::error_code& ec) noexcept {
#if defined(SEEK_HOLE) && defined(SEEK_DATA)
    assert(fd_ >= 0);

    off_t data_off = ::lseek(fd_, 0, SEEK_DATA);

    if (data_off < 0) {
      ec = std::error_code(errno, std::generic_category());
      return 0;
    }

    return static_cast<size_t>(data_off);
#else
    ec = std::make_error_code(std::errc::function_not_supported);
    return 0;
#endif
  }

 private:
  int fd_{-1};
};

#endif

std::optional<size_t> sparse_file_builder::hole_granularity(
    std::filesystem::path const& path) noexcept {
  std::error_code ec;
  auto builder = impl::create_temporary(path, ec);

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
                            std::error_code& ec) noexcept {
  return sparse_file_builder(impl::create(path, ec));
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

void sparse_file_builder::truncate(file_size_t size,
                                   std::error_code& ec) noexcept {
  assert(impl_);
  impl_->truncate(size, ec);
}

void sparse_file_builder::truncate(file_size_t size) {
  std::error_code ec;
  truncate(size, ec);
  throw_if_error(ec, "sparse_file_builder::truncate");
}

void sparse_file_builder::write_data(file_off_t offset, std::string_view data,
                                     std::error_code& ec) noexcept {
  assert(impl_);
  impl_->write_data(offset, data, ec);
}

void sparse_file_builder::write_data(file_off_t offset, std::string_view data) {
  std::error_code ec;
  write_data(offset, data, ec);
  throw_if_error(ec, "sparse_file_builder::write_data");
}

void sparse_file_builder::punch_hole(file_off_t offset, file_off_t size,
                                     std::error_code& ec) noexcept {
  assert(impl_);
  impl_->punch_hole(offset, size, ec);
}

void sparse_file_builder::punch_hole(file_off_t offset, file_off_t size) {
  std::error_code ec;
  punch_hole(offset, size, ec);
  throw_if_error(ec, "sparse_file_builder::punch_hole");
}

void sparse_file_builder::commit(std::error_code& ec) noexcept {
  assert(impl_);
  impl_->commit(ec);
}

void sparse_file_builder::commit() {
  std::error_code ec;
  commit(ec);
  throw_if_error(ec, "sparse_file_builder::commit");
}

} // namespace dwarfs::test
