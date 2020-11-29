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

#include <cerrno>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <boost/system/error_code.hpp>
#include <boost/system/system_error.hpp>

#include "dwarfs/mmap.h"

namespace dwarfs {

namespace {

int safe_open(const std::string& path) {
  int fd = ::open(path.c_str(), O_RDONLY);

  if (fd == -1) {
    throw boost::system::system_error(errno, boost::system::generic_category(),
                                      "open");
  }

  return fd;
}

size_t safe_size(int fd) {
  struct stat st;
  if (::fstat(fd, &st) == -1) {
    throw boost::system::system_error(errno, boost::system::generic_category(),
                                      "fstat");
  }
  return st.st_size;
}

void* safe_mmap(int fd, size_t size) {
  void* addr = ::mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);

  if (addr == MAP_FAILED) {
    throw boost::system::system_error(errno, boost::system::generic_category(),
                                      "mmap");
  }

  return addr;
}
} // namespace

mmap::mmap(const std::string& path)
    : fd_(safe_open(path)) {
  size_t size = safe_size(fd_);
  assign(safe_mmap(fd_, size), size);
}

mmap::mmap(const std::string& path, size_t size)
    : fd_(safe_open(path)) {
  assign(safe_mmap(fd_, size), size);
}

mmap::~mmap() noexcept {
  ::munmap(const_cast<void*>(get()), size());
  ::close(fd_);
}
} // namespace dwarfs
