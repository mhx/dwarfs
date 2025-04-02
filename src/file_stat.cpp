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
#include <ctime>
#include <filesystem>
#include <sstream>
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

#include <dwarfs/error.h>
#include <dwarfs/file_stat.h>
#include <dwarfs/mmap.h>
#include <dwarfs/util.h>

#include <dwarfs/internal/file_status_conv.h>

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

char get_filetype_label(file_stat::mode_type mode) {
  switch (posix_file_type::from_mode(mode)) {
  case posix_file_type::regular:
    return '-';
  case posix_file_type::directory:
    return 'd';
  case posix_file_type::symlink:
    return 'l';
  case posix_file_type::block:
    return 'b';
  case posix_file_type::character:
    return 'c';
  case posix_file_type::fifo:
    return 'p';
  case posix_file_type::socket:
    return 's';
  default:
    DWARFS_THROW(runtime_error,
                 fmt::format("unknown file type: {:#06x}", mode));
  }
}

void perms_to_stream(std::ostream& os, file_stat::mode_type mode) {
  os << (mode & file_stat::mode_type(fs::perms::owner_read) ? 'r' : '-');
  os << (mode & file_stat::mode_type(fs::perms::owner_write) ? 'w' : '-');
  os << (mode & file_stat::mode_type(fs::perms::owner_exec) ? 'x' : '-');
  os << (mode & file_stat::mode_type(fs::perms::group_read) ? 'r' : '-');
  os << (mode & file_stat::mode_type(fs::perms::group_write) ? 'w' : '-');
  os << (mode & file_stat::mode_type(fs::perms::group_exec) ? 'x' : '-');
  os << (mode & file_stat::mode_type(fs::perms::others_read) ? 'r' : '-');
  os << (mode & file_stat::mode_type(fs::perms::others_write) ? 'w' : '-');
  os << (mode & file_stat::mode_type(fs::perms::others_exec) ? 'x' : '-');
}

} // namespace

file_stat::file_stat() = default;

#ifdef _WIN32

file_stat::file_stat(fs::path const& path) {
  std::error_code ec;
  auto status = fs::symlink_status(path, ec);

  if (ec) {
    status = fs::status(path, ec);
  }

  if (ec) {
    exception_ = std::make_exception_ptr(std::system_error(ec));
    return;
  }

  if (status.type() == fs::file_type::not_found ||
      status.type() == fs::file_type::unknown) {
    DWARFS_THROW(runtime_error,
                 fmt::format("{}: {}",
                             status.type() == fs::file_type::not_found
                                 ? "file not found"
                                 : "unknown file type",
                             path_to_utf8_string_sanitized(path)));
  }

  valid_fields_ = file_stat::mode_valid;
  mode_ = internal::file_status_to_mode(status);
  blksize_ = 0;
  blocks_ = 0;

  auto wps = path.wstring();

  if (status.type() == fs::file_type::symlink) {
    ::WIN32_FILE_ATTRIBUTE_DATA info;
    if (::GetFileAttributesExW(wps.c_str(), GetFileExInfoStandard, &info) ==
        0) {
      exception_ = std::make_exception_ptr(std::system_error(
          ::GetLastError(), std::system_category(), "GetFileAttributesExW"));
    } else {
      valid_fields_ = file_stat::all_valid;
      dev_ = 0;
      ino_ = 0;
      nlink_ = 0;
      uid_ = 0;
      gid_ = 0;
      rdev_ = 0;
      size_ =
          (static_cast<uint64_t>(info.nFileSizeHigh) << 32) + info.nFileSizeLow;
      atime_ = time_from_filetime(info.ftLastAccessTime);
      mtime_ = time_from_filetime(info.ftLastWriteTime);
      ctime_ = time_from_filetime(info.ftCreationTime);
    }
  } else {
    struct ::__stat64 st;

    if (::_wstat64(wps.c_str(), &st) == 0) {
      if (status.type() == fs::file_type::regular) {
        ::HANDLE hdl =
            ::CreateFileW(wps.c_str(), 0, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                          FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, NULL);

        if (hdl != INVALID_HANDLE_VALUE) {
          ::BY_HANDLE_FILE_INFORMATION info;
          if (::GetFileInformationByHandle(hdl, &info)) {
            if (::CloseHandle(hdl)) {
              valid_fields_ |= file_stat::ino_valid | file_stat::nlink_valid;
              ino_ = (static_cast<uint64_t>(info.nFileIndexHigh) << 32) +
                     info.nFileIndexLow;
              nlink_ = info.nNumberOfLinks;
            } else {
              exception_ = std::make_exception_ptr(std::system_error(
                  ::GetLastError(), std::system_category(), "CloseHandle"));
            }
          } else {
            exception_ = std::make_exception_ptr(
                std::system_error(::GetLastError(), std::system_category(),
                                  "GetFileInformationByHandle"));
            ::CloseHandle(hdl);
          }
        } else {
          exception_ = std::make_exception_ptr(std::system_error(
              ::GetLastError(), std::system_category(), "CreateFileW"));
        }
      } else {
        valid_fields_ |= file_stat::ino_valid | file_stat::nlink_valid;
        ino_ = st.st_ino;
        nlink_ = st.st_nlink;
      }

      valid_fields_ |= file_stat::dev_valid | file_stat::uid_valid |
                       file_stat::gid_valid | file_stat::rdev_valid |
                       file_stat::size_valid | file_stat::atime_valid |
                       file_stat::mtime_valid | file_stat::ctime_valid;
      dev_ = st.st_dev;
      uid_ = st.st_uid;
      gid_ = st.st_gid;
      rdev_ = st.st_rdev;
      size_ = st.st_size;
      atime_ = st.st_atime;
      mtime_ = st.st_mtime;
      ctime_ = st.st_ctime;
    } else {
      exception_ = std::make_exception_ptr(
          std::system_error(errno, std::generic_category(), "_stat64"));
    }
  }
}

#else

file_stat::file_stat(fs::path const& path) {
  struct ::stat st;

  if (::lstat(path.string().c_str(), &st) != 0) {
    exception_ = std::make_exception_ptr(
        std::system_error(errno, std::generic_category(), "lstat"));
    return;
  }

  valid_fields_ = file_stat::all_valid;
  dev_ = st.st_dev;
  ino_ = st.st_ino;
  nlink_ = st.st_nlink;
  mode_ = st.st_mode;
  uid_ = st.st_uid;
  gid_ = st.st_gid;
  rdev_ = st.st_rdev;
  size_ = st.st_size;
  blksize_ = st.st_blksize;
  blocks_ = st.st_blocks;
#ifdef __APPLE__
  atime_ = st.st_atimespec.tv_sec;
  mtime_ = st.st_mtimespec.tv_sec;
  ctime_ = st.st_ctimespec.tv_sec;
#else
  atime_ = st.st_atim.tv_sec;
  mtime_ = st.st_mtim.tv_sec;
  ctime_ = st.st_ctim.tv_sec;
#endif
}

#endif

std::string file_stat::mode_string(mode_type mode) {
  std::ostringstream oss;

  oss << (mode & mode_type(fs::perms::set_uid) ? 'U' : '-');
  oss << (mode & mode_type(fs::perms::set_gid) ? 'G' : '-');
  oss << (mode & mode_type(fs::perms::sticky_bit) ? 'S' : '-');
  oss << get_filetype_label(mode);
  perms_to_stream(oss, mode);

  return oss.str();
}

std::string file_stat::perm_string(mode_type mode) {
  std::ostringstream oss;
  perms_to_stream(oss, mode);
  return oss.str();
}

void file_stat::ensure_valid(valid_fields_type fields) const {
  if ((valid_fields_ & fields) != fields) {
    if (exception_) {
      std::rethrow_exception(exception_);
    } else {
      DWARFS_THROW(runtime_error,
                   fmt::format("missing stat fields: {:#x} (have: {:#x})",
                               fields, valid_fields_));
    }
  }
}

std::filesystem::file_status file_stat::status() const {
  ensure_valid(mode_valid);
  return internal::file_mode_to_status(mode_);
};

posix_file_type::value file_stat::type() const {
  ensure_valid(mode_valid);
  return static_cast<posix_file_type::value>(mode_ & posix_file_type::mask);
};

file_stat::perms_type file_stat::permissions() const {
  ensure_valid(mode_valid);
  return mode_ & 07777;
};

void file_stat::set_permissions(perms_type perms) {
  mode_ = type() | (perms & 07777);
}

bool file_stat::is_directory() const {
  return type() == posix_file_type::directory;
}

bool file_stat::is_regular_file() const {
  return type() == posix_file_type::regular;
}

bool file_stat::is_symlink() const {
  return type() == posix_file_type::symlink;
}

bool file_stat::is_device() const {
  auto t = type();
  return t == posix_file_type::block || t == posix_file_type::character;
}

file_stat::dev_type file_stat::dev() const {
  ensure_valid(dev_valid);
  return dev_;
}

void file_stat::set_dev(dev_type dev) {
  valid_fields_ |= dev_valid;
  dev_ = dev;
}

file_stat::ino_type file_stat::ino() const {
  ensure_valid(ino_valid);
  return ino_;
}

void file_stat::set_ino(ino_type ino) {
  valid_fields_ |= ino_valid;
  ino_ = ino;
}

file_stat::nlink_type file_stat::nlink() const {
  ensure_valid(nlink_valid);
  return nlink_;
}

void file_stat::set_nlink(nlink_type nlink) {
  valid_fields_ |= nlink_valid;
  nlink_ = nlink;
}

file_stat::mode_type file_stat::mode() const {
  ensure_valid(mode_valid);
  return mode_;
}

void file_stat::set_mode(mode_type mode) {
  valid_fields_ |= mode_valid;
  mode_ = mode;
}

file_stat::uid_type file_stat::uid() const {
  ensure_valid(uid_valid);
  return uid_;
}

void file_stat::set_uid(uid_type uid) {
  valid_fields_ |= uid_valid;
  uid_ = uid;
}

file_stat::gid_type file_stat::gid() const {
  ensure_valid(gid_valid);
  return gid_;
}

void file_stat::set_gid(gid_type gid) {
  valid_fields_ |= gid_valid;
  gid_ = gid;
}

file_stat::dev_type file_stat::rdev() const {
  ensure_valid(rdev_valid);
  return rdev_;
}

void file_stat::set_rdev(dev_type rdev) {
  valid_fields_ |= rdev_valid;
  rdev_ = rdev;
}

file_stat::off_type file_stat::size() const {
  ensure_valid(size_valid);
  return size_;
}

void file_stat::set_size(off_type size) {
  valid_fields_ |= size_valid;
  size_ = size;
}

file_stat::blksize_type file_stat::blksize() const {
  ensure_valid(blksize_valid);
  return blksize_;
}

void file_stat::set_blksize(blksize_type blksize) {
  valid_fields_ |= blksize_valid;
  blksize_ = blksize;
}

file_stat::blkcnt_type file_stat::blocks() const {
  ensure_valid(blocks_valid);
  return blocks_;
}

void file_stat::set_blocks(blkcnt_type blocks) {
  valid_fields_ |= blocks_valid;
  blocks_ = blocks;
}

file_stat::time_type file_stat::atime() const {
  ensure_valid(atime_valid);
  return atime_;
}

void file_stat::set_atime(time_type atime) {
  valid_fields_ |= atime_valid;
  atime_ = atime;
}

file_stat::time_type file_stat::mtime() const {
  ensure_valid(mtime_valid);
  return mtime_;
}

void file_stat::set_mtime(time_type mtime) {
  valid_fields_ |= mtime_valid;
  mtime_ = mtime;
}

file_stat::time_type file_stat::ctime() const {
  ensure_valid(ctime_valid);
  return ctime_;
}

void file_stat::set_ctime(time_type ctime) {
  valid_fields_ |= ctime_valid;
  ctime_ = ctime;
}

} // namespace dwarfs
