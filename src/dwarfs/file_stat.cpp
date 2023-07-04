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
#include <ctime>
#include <filesystem>
#include <stdexcept>
#include <vector>

#ifdef _WIN32
#include <folly/portability/Windows.h>
#else
#include <fcntl.h>
#endif

#include <folly/portability/Unistd.h>

#include <sys/stat.h>
#include <sys/types.h>

#include <fmt/format.h>

#include "dwarfs/error.h"
#include "dwarfs/mmap.h"
#include "dwarfs/os_access_generic.h"
#include "dwarfs/util.h"

namespace dwarfs {

namespace {

namespace fs = std::filesystem;

#ifdef _WIN32

uint64_t time_from_filetime(FILETIME const& ft) {
  static constexpr uint64_t FT_TICKS_PER_SECOND = UINT64_C(10000000);
  static constexpr uint64_t FT_EPOCH_OFFSET = UINT64_C(11644473600);
  uint64_t ticks =
      (static_cast<uint64_t>(ft.dwHighDateTime) << 32) + ft.dwLowDateTime;
  return (ticks / FT_TICKS_PER_SECOND) - FT_EPOCH_OFFSET;
}

#endif

} // namespace

#ifdef _WIN32

file_stat make_file_stat(fs::path const& path) {
  auto status = fs::symlink_status(path);

  DWARFS_CHECK(status.type() != fs::file_type::not_found,
               u8string_to_string(path.u8string()));

  file_stat rv;
  rv.mode = file_status_to_mode(status);
  rv.blksize = 0;
  rv.blocks = 0;

  auto wps = path.wstring();

  if (status.type() == fs::file_type::symlink) {
    ::WIN32_FILE_ATTRIBUTE_DATA info;
    if (::GetFileAttributesExW(wps.c_str(), GetFileExInfoStandard, &info) ==
        0) {
      throw std::system_error(::GetLastError(), std::system_category(),
                              "GetFileAttributesExW");
    }
    rv.dev = 0;
    rv.ino = 0;
    rv.nlink = 0;
    rv.uid = 0;
    rv.gid = 0;
    rv.rdev = 0;
    rv.size =
        (static_cast<uint64_t>(info.nFileSizeHigh) << 32) + info.nFileSizeLow;
    rv.atime = time_from_filetime(info.ftLastAccessTime);
    rv.mtime = time_from_filetime(info.ftLastWriteTime);
    rv.ctime = time_from_filetime(info.ftCreationTime);
  } else {
    struct ::__stat64 st;

    if (::_wstat64(wps.c_str(), &st) != 0) {
      throw std::system_error(errno, std::generic_category(), "_stat64");
    }

    if (status.type() == fs::file_type::regular) {
      ::HANDLE hdl =
          ::CreateFileW(wps.c_str(), 0, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, NULL);

      if (hdl == INVALID_HANDLE_VALUE) {
        throw std::system_error(::GetLastError(), std::system_category(),
                                fmt::format("CreateFileW({})", path.string()));
      }

      ::BY_HANDLE_FILE_INFORMATION info;
      if (!::GetFileInformationByHandle(hdl, &info)) {
        throw std::system_error(::GetLastError(), std::system_category(),
                                "GetFileInformationByHandle");
      }

      if (!::CloseHandle(hdl)) {
        throw std::system_error(::GetLastError(), std::system_category(),
                                "CloseHandle");
      }

      rv.ino = (static_cast<uint64_t>(info.nFileIndexHigh) << 32) +
               info.nFileIndexLow;
      rv.nlink = info.nNumberOfLinks;
    } else {
      rv.ino = st.st_ino;
      rv.nlink = st.st_nlink;
    }

    rv.dev = st.st_dev;
    rv.uid = st.st_uid;
    rv.gid = st.st_gid;
    rv.rdev = st.st_rdev;
    rv.size = st.st_size;
    rv.atime = st.st_atime;
    rv.mtime = st.st_mtime;
    rv.ctime = st.st_ctime;
  }

  return rv;
}

#else

file_stat make_file_stat(fs::path const& path) {
  struct ::stat st;

  if (::lstat(path.string().c_str(), &st) != 0) {
    throw std::system_error(errno, std::generic_category(), "lstat");
  }

  file_stat rv;
  rv.dev = st.st_dev;
  rv.ino = st.st_ino;
  rv.nlink = st.st_nlink;
  rv.mode = st.st_mode;
  rv.uid = st.st_uid;
  rv.gid = st.st_gid;
  rv.rdev = st.st_rdev;
  rv.size = st.st_size;
  rv.blksize = st.st_blksize;
  rv.blocks = st.st_blocks;
  rv.atime = st.st_atim.tv_sec;
  rv.mtime = st.st_mtim.tv_sec;
  rv.ctime = st.st_ctim.tv_sec;

  return rv;
}

#endif

} // namespace dwarfs
