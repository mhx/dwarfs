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
 */

#include <folly/portability/Fcntl.h>
#include <folly/portability/SysMman.h>
#include <folly/portability/SysStat.h>
#include <folly/portability/Unistd.h>

#include <cerrno>

#include <boost/system/error_code.hpp>

#include <fmt/format.h>

#include "dwarfs/error.h"
#include "dwarfs/mmap.h"

namespace dwarfs {

namespace {

int safe_open(const std::string& path) {
  int fd = ::open(path.c_str(), O_RDONLY);

  if (fd == -1) {
    DWARFS_THROW(system_error, fmt::format("open('{}')", path));
  }

  return fd;
}

size_t safe_size(int fd) {
  struct stat st;
  if (::fstat(fd, &st) == -1) {
    DWARFS_THROW(system_error, "fstat");
  }
  return st.st_size;
}

void* safe_mmap(int fd, size_t size) {
  if (size == 0) {
    DWARFS_THROW(runtime_error, "empty file");
  }

  void* addr = ::mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);

  if (addr == MAP_FAILED) {
    DWARFS_THROW(system_error, "mmap");
  }

  return addr;
}

} // namespace

boost::system::error_code mmap::lock(off_t offset, size_t size) {
  boost::system::error_code ec;
  auto addr = reinterpret_cast<uint8_t*>(addr_) + offset;
  if (::mlock(addr, size) != 0) {
    ec.assign(errno, boost::system::generic_category());
  }
  return ec;
}

boost::system::error_code mmap::release(off_t offset, size_t size) {
  boost::system::error_code ec;
  auto misalign = offset % page_size_;

  offset -= misalign;
  size += misalign;
  size -= size % page_size_;

  auto addr = reinterpret_cast<uint8_t*>(addr_) + offset;
  if (::madvise(addr, size, MADV_DONTNEED) != 0) {
    ec.assign(errno, boost::system::generic_category());
  }
  return ec;
}

boost::system::error_code mmap::release_until(off_t offset) {
  boost::system::error_code ec;

  offset -= offset % page_size_;

  if (::madvise(addr_, offset, MADV_DONTNEED) != 0) {
    ec.assign(errno, boost::system::generic_category());
  }
  return ec;
}

void const* mmap::addr() const { return addr_; }

size_t mmap::size() const { return size_; }

mmap::mmap(const std::string& path)
    : fd_(safe_open(path))
    , size_(safe_size(fd_))
    , addr_(safe_mmap(fd_, size_))
    , page_size_(::sysconf(_SC_PAGESIZE)) {}

mmap::mmap(const std::string& path, size_t size)
    : fd_(safe_open(path))
    , size_(size)
    , addr_(safe_mmap(fd_, size_))
    , page_size_(::sysconf(_SC_PAGESIZE)) {}

mmap::~mmap() noexcept {
  ::munmap(addr_, size_);
  ::close(fd_);
}
} // namespace dwarfs
