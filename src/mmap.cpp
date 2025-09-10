/* vim:set ts=2 sw=2 sts=2 et: */
/**
 * \author     Marcus Holland-Moritz (github@mhxnet.de)
 * \copyright  Copyright (c) Marcus Holland-Moritz
 *
 * This file is part of dwarfs.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the “Software”), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */

#include <cerrno>

#ifdef _WIN32
#include <boost/filesystem/path.hpp>
#include <folly/portability/Windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

#include <boost/iostreams/device/mapped_file.hpp>

#include <dwarfs/binary_literals.h>
#include <dwarfs/error.h>
#include <dwarfs/mmap.h>
#include <dwarfs/scope_exit.h>

namespace dwarfs {

namespace {

using namespace binary_literals;

#if defined(SEEK_HOLE) && defined(SEEK_DATA)

std::vector<detail::file_extent_info>
get_file_extents(std::filesystem::path const& path, std::error_code& ec) {
  std::vector<detail::file_extent_info> extents;

  auto fd = ::open(path.c_str(), O_RDONLY);
  if (fd < 0) {
    ec.assign(errno, std::generic_category());
    return extents;
  }

  scope_exit close_fd{[fd]() { ::close(fd); }};

  int whence = SEEK_DATA;
  off_t offset = 0;

  for (;;) {
    off_t rv = ::lseek(fd, offset, whence);

    if (rv < 0) {
      if (errno != ENXIO) {
        ec.assign(errno, std::generic_category());
      }
      break;
    }

    extent_kind kind;

    switch (whence) {
    case SEEK_DATA:
      kind = extent_kind::hole;
      whence = SEEK_HOLE;
      break;
    case SEEK_HOLE:
      kind = extent_kind::data;
      whence = SEEK_DATA;
      break;
    default:
      DWARFS_PANIC("invalid whence");
    }

    if (rv > offset) {
      extents.emplace_back(kind, offset, rv - offset);
      offset = rv;
    }
  }

  return extents;
}

std::vector<detail::file_extent_info>
get_file_extents(std::filesystem::path const& path) {
  std::error_code ec;
  auto extents = get_file_extents(path, ec);
  if (ec) {
    extents.clear();
    extents.emplace_back(extent_kind::data, 0,
                         std::filesystem::file_size(path));
  }
  return extents;
}

#endif

class mmap_file_view : public detail::file_view_impl,
                       public std::enable_shared_from_this<mmap_file_view> {
 public:
  explicit mmap_file_view(std::filesystem::path const& path);
  mmap_file_view(std::filesystem::path const& path, file_size_t size);

  file_size_t size() const override;

  file_segment segment_at(file_off_t offset, size_t size) const override;

  file_extents_iterable extents() const override;

  bool supports_raw_bytes() const noexcept override;

  std::span<std::byte const> raw_bytes() const override;

  void copy_bytes(void* dest, file_off_t offset, size_t size,
                  std::error_code& ec) const override;

  size_t default_segment_size() const override { return 16_MiB; }

  // ------------------------------------------------------------

  std::error_code release_until(file_off_t offset) const override;

  // ------------------------------------------------------------

  std::filesystem::path const& path() const override;

  // Not exposed publicly
  void const* addr() const { return mf_.const_data(); }

  std::error_code advise(io_advice adv, file_off_t offset, size_t size) const;

  std::error_code lock(file_off_t offset, size_t size) const;

 private:
  boost::iostreams::mapped_file mutable mf_;
  uint64_t const page_size_;
  std::filesystem::path const path_;
  std::vector<detail::file_extent_info> const extents_;
};

class mmap_ref_file_segment : public detail::file_segment_impl {
 public:
  mmap_ref_file_segment(std::shared_ptr<mmap_file_view const> const& mm,
                        file_off_t offset, size_t size)
      : mm_{mm}
      , offset_{offset}
      , size_{size} {}

  file_off_t offset() const noexcept override { return offset_; }
  size_t size() const noexcept override { return size_; }

  bool is_zero() const noexcept override { return false; }

  std::span<std::byte const> raw_bytes() const override {
    return {static_cast<std::byte const*>(mm_->addr()) + offset_, size_};
  }

  void advise(io_advice adv, file_off_t offset, size_t size,
              std::error_code& ec) const override {
    ec = mm_->advise(adv, offset_ + offset, size);
  }

  void lock(std::error_code& ec) const override {
    ec = mm_->lock(offset_, size_);
  }

 private:
  std::shared_ptr<mmap_file_view const> mm_;
  file_off_t offset_;
  size_t size_;
};

uint64_t get_page_size() {
#ifdef _WIN32
  ::SYSTEM_INFO info;
  ::GetSystemInfo(&info);
  return info.dwPageSize;
#else
  return ::sysconf(_SC_PAGESIZE);
#endif
}

#ifndef _WIN32
int posix_advice(io_advice adv) {
  switch (adv) {
  case io_advice::normal:
    return MADV_NORMAL;
  case io_advice::random:
    return MADV_RANDOM;
  case io_advice::sequential:
    return MADV_SEQUENTIAL;
  case io_advice::willneed:
    return MADV_WILLNEED;
  case io_advice::dontneed:
    return MADV_DONTNEED;
  }

  DWARFS_PANIC("invalid advice");

  return MADV_NORMAL;
}
#endif

decltype(auto) get_file_path(std::filesystem::path const& path) {
#ifdef _WIN32
  return boost::filesystem::path{path.native()};
#else
  return path.native();
#endif
}

file_segment mmap_file_view::segment_at(file_off_t offset, size_t size) const {
  if (offset < 0 || size == 0 || offset + size > mf_.size()) {
    return {};
  }

  return file_segment(std::make_shared<mmap_ref_file_segment>(
      shared_from_this(), offset, size));
}

file_extents_iterable mmap_file_view::extents() const {
  return file_extents_iterable(shared_from_this(), extents_);
}

bool mmap_file_view::supports_raw_bytes() const noexcept { return true; }

std::span<std::byte const> mmap_file_view::raw_bytes() const {
  return {reinterpret_cast<std::byte const*>(mf_.const_data()), mf_.size()};
}

void mmap_file_view::copy_bytes(void* dest, file_off_t offset, size_t size,
                                std::error_code& ec) const {
  if (size == 0) {
    return;
  }

  if (dest == nullptr || offset < 0) {
    ec = make_error_code(std::errc::invalid_argument);
    return;
  }

  if (offset + size > mf_.size()) {
    ec = make_error_code(std::errc::result_out_of_range);
    return;
  }

  std::memcpy(dest, mf_.const_data() + offset, size);
}

std::error_code mmap_file_view::lock(file_off_t offset [[maybe_unused]],
                                     size_t size [[maybe_unused]]) const {
  std::error_code ec;

  auto data = mf_.const_data() + offset;

#ifdef _WIN32
  if (::VirtualLock(const_cast<char*>(data), size) == 0) {
    ec.assign(::GetLastError(), std::system_category());
  }
#else
  if (::mlock(data, size) != 0) {
    ec.assign(errno, std::generic_category());
  }
#endif

  return ec;
}

std::error_code mmap_file_view::advise(io_advice adv [[maybe_unused]],
                                       file_off_t offset [[maybe_unused]],
                                       size_t size [[maybe_unused]]) const {
  std::error_code ec;

#ifdef _WIN32
  //// TODO: this doesn't currently work
  // if (::VirtualFree(data, size, MEM_DECOMMIT) == 0) {
  //   ec.assign(::GetLastError(), std::system_category());
  // }
#else
  auto misalign = offset % page_size_;

  offset -= misalign;
  size += misalign;
  size -= size % page_size_;

  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
  auto data = const_cast<char*>(mf_.const_data() + offset);

  int native_adv = posix_advice(adv);

  if (::madvise(data, size, native_adv) != 0) {
    ec.assign(errno, std::generic_category());
  }
#endif

  return ec;
}

std::error_code mmap_file_view::release_until(file_off_t offset) const {
  return advise(io_advice::dontneed, 0, offset);
}

file_size_t mmap_file_view::size() const { return mf_.size(); }

std::filesystem::path const& mmap_file_view::path() const { return path_; }

mmap_file_view::mmap_file_view(std::filesystem::path const& path)
    : mf_{get_file_path(path), boost::iostreams::mapped_file::readonly}
    , page_size_{get_page_size()}
    , path_{path}
    , extents_{get_file_extents(path)} {
  DWARFS_CHECK(mf_.is_open(), "failed to map file");
}

mmap_file_view::mmap_file_view(std::filesystem::path const& path,
                               file_size_t size)
    : mf_{get_file_path(path), boost::iostreams::mapped_file::readonly,
          static_cast<size_t>(size)}
    , page_size_{get_page_size()}
    , path_{path}
    , extents_{get_file_extents(path)} {
  DWARFS_CHECK(mf_.is_open(), "failed to map file");
}

} // namespace

file_view create_mmap_file_view(std::filesystem::path const& path) {
  return file_view(std::make_shared<mmap_file_view>(path));
}

file_view
create_mmap_file_view(std::filesystem::path const& path, file_size_t size) {
  return file_view(std::make_shared<mmap_file_view>(path, size));
}

} // namespace dwarfs
