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

#ifdef _WIN32
#include <folly/portability/Windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

#include <boost/filesystem/path.hpp>

#include "dwarfs/error.h"
#include "dwarfs/mmap.h"

namespace dwarfs {

namespace {

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
int posix_advice(advice adv) {
  switch (adv) {
  case advice::normal:
    return MADV_NORMAL;
  case advice::random:
    return MADV_RANDOM;
  case advice::sequential:
    return MADV_SEQUENTIAL;
  case advice::willneed:
    return MADV_WILLNEED;
  case advice::dontneed:
    return MADV_DONTNEED;
  }

  assert(false);

  return MADV_NORMAL;
}
#endif

boost::filesystem::path boost_from_std_path(std::filesystem::path const& p) {
#ifdef _WIN32
  return boost::filesystem::path(p.wstring());
#else
  return boost::filesystem::path(p.string());
#endif
}

} // namespace

std::error_code
mmap::lock(file_off_t offset [[maybe_unused]], size_t size [[maybe_unused]]) {
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

std::error_code
mmap::advise(advice adv [[maybe_unused]], file_off_t offset [[maybe_unused]],
             size_t size [[maybe_unused]]) {
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

  auto data = const_cast<char*>(mf_.const_data() + offset);

  int native_adv = posix_advice(adv);

  if (::madvise(data, size, native_adv) != 0) {
    ec.assign(errno, std::generic_category());
  }
#endif

  return ec;
}

std::error_code mmap::advise(advice adv) { return advise(adv, 0, size()); }

std::error_code mmap::release(file_off_t offset, size_t size) {
  return advise(advice::dontneed, offset, size);
}

std::error_code mmap::release_until(file_off_t offset) {
  return release(0, offset);
}

void const* mmap::addr() const { return mf_.const_data(); }

size_t mmap::size() const { return mf_.size(); }

std::filesystem::path const& mmap::path() const { return path_; }

mmap::mmap(char const* path)
    : mmap(std::filesystem::path(path)) {}

mmap::mmap(std::string const& path)
    : mmap(std::filesystem::path(path)) {}

mmap::mmap(std::filesystem::path const& path)
    : mf_(boost_from_std_path(path), boost::iostreams::mapped_file::readonly)
    , page_size_(get_page_size())
    , path_{path} {
  assert(mf_.is_open());
}

mmap::mmap(std::string const& path, size_t size)
    : mmap(std::filesystem::path(path), size) {}

mmap::mmap(std::filesystem::path const& path, size_t size)
    : mf_(boost_from_std_path(path), boost::iostreams::mapped_file::readonly,
          size)
    , page_size_(get_page_size())
    , path_{path} {
  assert(mf_.is_open());
}

} // namespace dwarfs
