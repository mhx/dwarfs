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
#include <sys/mman.h>
#include <unistd.h>
#endif

#include <dwarfs/error.h>
#include <dwarfs/mmap.h>

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

  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
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

mmap::mmap(std::filesystem::path const& path)
    : mf_{get_file_path(path), boost::iostreams::mapped_file::readonly}
    , page_size_{get_page_size()}
    , path_{path} {
  DWARFS_CHECK(mf_.is_open(), "failed to map file");
}

mmap::mmap(std::filesystem::path const& path, size_t size)
    : mf_{get_file_path(path), boost::iostreams::mapped_file::readonly, size}
    , page_size_{get_page_size()}
    , path_{path} {
  DWARFS_CHECK(mf_.is_open(), "failed to map file");
}

} // namespace dwarfs
