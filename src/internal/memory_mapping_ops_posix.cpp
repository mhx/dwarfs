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

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <dwarfs/error.h>

#include <dwarfs/detail/file_extent_info.h>

#include <dwarfs/internal/memory_mapping_ops.h>

namespace dwarfs::internal {

using dwarfs::detail::file_extent_info;

namespace {

long get_page_size() {
  static long const page_size = ::sysconf(_SC_PAGESIZE);
  return page_size;
}

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

#if defined(SEEK_HOLE) && defined(SEEK_DATA)

std::vector<file_extent_info> get_file_extents(int fd, std::error_code& ec) {
  std::vector<file_extent_info> extents;

  int whence = SEEK_DATA;
  off_t offset = 0;

  for (;;) {
    off_t rv = ::lseek(fd, offset, whence);

    if (rv < 0) {
      if (errno != ENXIO) {
        ec.assign(errno, std::generic_category());
        return {};
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
      extents.emplace_back(kind, file_range{offset, rv - offset});
      offset = rv;
    }
  }

  off_t rv = ::lseek(fd, 0, SEEK_END);

  if (rv < 0) {
    ec.assign(errno, std::generic_category());
    return {};
  }

  if (rv > offset) {
    extents.emplace_back(extent_kind::hole, file_range{offset, rv - offset});
  }

  return extents;
}

#else

std::vector<file_extent_info>
get_file_extents(int /*fd*/, std::error_code& ec) {
  ec = make_error_code(std::errc::not_supported);
  return {};
}

#endif

class memory_mapping_ops_posix : public memory_mapping_ops {
 public:
  struct posix_handle {
    int fd;
    uint64_t size;
  };

  std::any
  open(std::filesystem::path const& path, std::error_code& ec) const override {
    ec.clear();

    // NOLINTNEXTLINE: cppcoreguidelines-pro-type-vararg
    int fd = ::open(path.c_str(), O_RDONLY);

    if (fd == -1) {
      ec = std::error_code{errno, std::generic_category()};
      return {};
    }

    auto const size = ::lseek(fd, 0, SEEK_END);

    if (size == -1) {
      ec = std::error_code{errno, std::generic_category()};
      ::close(fd);
      return {};
    }

    return posix_handle{fd, static_cast<uint64_t>(size)};
  }

  void close(std::any const& handle, std::error_code& ec) const override {
    if (auto const* h = get_handle(handle, ec)) {
      if (::close(h->fd) != 0) {
        ec = std::error_code{errno, std::generic_category()};
      }
    }
  }

  file_size_t size(std::any const& handle, std::error_code& ec) const override {
    if (auto const* h = get_handle(handle, ec)) {
      return h->size;
    }

    return 0;
  }

  size_t granularity() const override {
    return static_cast<size_t>(get_page_size());
  }

  std::vector<file_extent_info>
  get_extents(std::any const& handle, std::error_code& ec) const override {
    if (auto const* h = get_handle(handle, ec)) {
      return get_file_extents(h->fd, ec);
    }

    return {};
  }

  size_t pread(std::any const& handle, void* buf, size_t size,
               file_off_t offset, std::error_code& ec) const override {
    if (auto const* h = get_handle(handle, ec)) {
      auto const rv = ::pread(h->fd, buf, size, offset);

      if (rv == -1) {
        ec = std::error_code{errno, std::generic_category()};
        return 0;
      }

      return static_cast<size_t>(rv);
    }

    return 0;
  }

  void* virtual_alloc(size_t size, memory_access access,
                      std::error_code& ec) const override {
    int const prot =
        access == memory_access::readonly ? PROT_READ : PROT_READ | PROT_WRITE;
    auto const addr =
        ::mmap(nullptr, size, prot, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    if (addr == MAP_FAILED) {
      ec = std::error_code{errno, std::generic_category()};
      return {};
    }

    return addr;
  }

  void
  virtual_free(void* addr, size_t size, std::error_code& ec) const override {
    if (::munmap(addr, size) != 0) {
      ec = std::error_code{errno, std::generic_category()};
    }
  }

  void* map(std::any const& handle, file_off_t offset, size_t size,
            std::error_code& ec) const override {
    if (offset < 0) {
      ec = std::make_error_code(std::errc::invalid_argument);
      return nullptr;
    }

    if (auto const* h = get_handle(handle, ec)) {
      auto const addr =
          ::mmap(nullptr, size, PROT_READ, MAP_PRIVATE, h->fd, offset);

      if (addr == MAP_FAILED) {
        ec = std::error_code{errno, std::generic_category()};
        return nullptr;
      }

      return addr;
    }

    return nullptr;
  }

  void unmap(void* addr, size_t size, std::error_code& ec) const override {
    if (::munmap(addr, size) != 0) {
      ec = std::error_code{errno, std::generic_category()};
    }
  }

  void advise(void* addr, size_t size, io_advice advice,
              std::error_code& ec) const override {
    auto const native_advice = posix_advice(advice);

    if (::madvise(addr, size, native_advice) != 0) {
      ec = std::error_code{errno, std::generic_category()};
    }
  }

  void lock(void* addr, size_t size, std::error_code& ec) const override {
    if (::mlock(addr, size) != 0) {
      ec = std::error_code{errno, std::generic_category()};
    }
  }

 private:
  posix_handle const*
  get_handle(std::any const& handle, std::error_code& ec) const {
    auto const* h = std::any_cast<posix_handle>(&handle);

    if (!h) {
      ec = std::make_error_code(std::errc::bad_file_descriptor);
    }

    return h;
  }
};

} // namespace

memory_mapping_ops const& get_native_memory_mapping_ops() {
  static memory_mapping_ops_posix const ops;
  return ops;
}

} // namespace dwarfs::internal
