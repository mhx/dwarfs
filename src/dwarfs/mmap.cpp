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

#include <cassert>
#include <cerrno>

#ifndef _WIN32
#include <sys/mman.h>
#endif

#include "dwarfs/error.h"
#include "dwarfs/mmap.h"

namespace dwarfs {

std::error_code
mmap::lock(off_t offset [[maybe_unused]], size_t size [[maybe_unused]]) {
  std::error_code ec;

#ifndef _WIN32
  if (::mlock(mf_.const_data() + offset, size) != 0) {
    ec.assign(errno, std::generic_category());
  }
#endif

  return ec;
}

std::error_code
mmap::release(off_t offset [[maybe_unused]], size_t size [[maybe_unused]]) {
  std::error_code ec;

#ifndef _WIN32
  auto misalign = offset % page_size_;

  offset -= misalign;
  size += misalign;
  size -= size % page_size_;

  if (::madvise(mf_.data() + offset, size, MADV_DONTNEED) != 0) {
    ec.assign(errno, std::generic_category());
  }
#endif

  return ec;
}

std::error_code mmap::release_until(off_t offset [[maybe_unused]]) {
  std::error_code ec;

#ifndef _WIN32
  offset -= offset % page_size_;

  if (::madvise(mf_.data(), offset, MADV_DONTNEED) != 0) {
    ec.assign(errno, std::generic_category());
  }
#endif

  return ec;
}

void const* mmap::addr() const { return mf_.const_data(); }

size_t mmap::size() const { return mf_.size(); }

mmap::mmap(const std::string& path)
    : mf_(path, boost::iostreams::mapped_file::readonly)
#ifndef _WIN32
    , page_size_(::sysconf(_SC_PAGESIZE))
#endif
{
  assert(mf_.is_open());
}

mmap::mmap(const std::string& path, size_t size)
    : mf_(path, boost::iostreams::mapped_file::readonly, size)
#ifndef _WIN32
    , page_size_(::sysconf(_SC_PAGESIZE))
#endif
{
  assert(mf_.is_open());
}

} // namespace dwarfs
