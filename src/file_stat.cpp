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

#include <algorithm>
#include <array>
#include <cerrno>
#include <ctime>
#include <filesystem>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <vector>

#ifdef _WIN32
#include <folly/portability/Windows.h>
#include <winioctl.h>
#else
#include <fcntl.h>
#endif

#include <folly/portability/Unistd.h>

#include <sys/stat.h>
#include <sys/types.h>

#include <fmt/format.h>

#include <dwarfs/error.h>
#include <dwarfs/file_stat.h>
#include <dwarfs/scope_exit.h>
#include <dwarfs/util.h>

#include <dwarfs/internal/file_status_conv.h>

namespace dwarfs {

namespace {

namespace fs = std::filesystem;

#ifdef _WIN32

file_stat::timespec_type to_unix_timespec(LARGE_INTEGER const& ft) {
  // FILETIME epoch (1601) → Unix epoch (1970)
  static constexpr int64_t kEpochDiff100ns = INT64_C(116'444'736'000'000'000);
  auto const t100 = static_cast<int64_t>(ft.QuadPart);
  file_stat::timespec_type ts{};
  if (t100 >= kEpochDiff100ns) {
    ts.sec = (t100 - kEpochDiff100ns) / INT64_C(10'000'000);
    ts.nsec = static_cast<uint32_t>((t100 % INT64_C(10'000'000)) * 100);
  }
  return ts;
};

int utf8_len_of_print_name(WCHAR const* wstr, int wlen_chars) {
  if (!wstr || wlen_chars <= 0) {
    return 0;
  }

  int need = ::WideCharToMultiByte(CP_UTF8, 0, wstr, wlen_chars, nullptr, 0,
                                   nullptr, nullptr);
  return std::max(0, need);
};

bool is_executable(fs::path const& path) {
  static constexpr std::array executable_exts{
      std::wstring_view{L".exe"},
      std::wstring_view{L".com"},
      std::wstring_view{L".bat"},
      std::wstring_view{L".cmd"},
  };

  return std::ranges::any_of(
      executable_exts,
      [ext = path.extension().wstring()](auto const& e) { return ext == e; });
}

file_stat::off_type
get_sparse_file_allocated_size_win(HANDLE h, file_stat::off_type logical_size) {
  FILE_ALLOCATED_RANGE_BUFFER in{};
  in.FileOffset.QuadPart = 0;
  in.Length.QuadPart = logical_size;

  file_stat::off_type total_allocated{0};
  DWORD bytes{};
  std::array<FILE_ALLOCATED_RANGE_BUFFER, 16> buf;

  for (;;) {
    auto const done =
        ::DeviceIoControl(h, FSCTL_QUERY_ALLOCATED_RANGES, &in, sizeof(in),
                          buf.data(), sizeof(buf), &bytes, nullptr);
    bool const more = done == 0 && ::GetLastError() == ERROR_MORE_DATA;

    if (!done && !more) {
      return logical_size; // fallback
    }

    size_t const num = bytes / sizeof(FILE_ALLOCATED_RANGE_BUFFER);

    for (size_t i = 0; i < num; ++i) {
      total_allocated += buf[i].Length.QuadPart;
    }

    if (done) {
      break;
    }

    in.FileOffset.QuadPart =
        buf[num - 1].FileOffset.QuadPart + buf[num - 1].Length.QuadPart;
  }

  return total_allocated;
}

// REPARSE_DATA_BUFFER isn't exposed by windows.h, so define a minimal
// compatible version here.
typedef struct _REPARSE_DATA_BUFFER {
  ULONG ReparseTag;
  USHORT ReparseDataLength;
  USHORT Reserved;
  union {
    struct {
      USHORT SubstituteNameOffset;
      USHORT SubstituteNameLength;
      USHORT PrintNameOffset;
      USHORT PrintNameLength;
      ULONG Flags;
      WCHAR PathBuffer[1];
    } SymbolicLinkReparseBuffer;
    struct {
      USHORT SubstituteNameOffset;
      USHORT SubstituteNameLength;
      USHORT PrintNameOffset;
      USHORT PrintNameLength;
      WCHAR PathBuffer[1];
    } MountPointReparseBuffer;
    struct {
      UCHAR DataBuffer[1];
    } GenericReparseBuffer;
  };
} REPARSE_DATA_BUFFER;

#else

file_stat::off_type
get_sparse_file_allocated_size_posix(fs::path const& path [[maybe_unused]],
                                     file_stat::off_type logical_size) {
#if defined(SEEK_HOLE) && defined(SEEK_DATA)
  // NOLINTNEXTLINE: cppcoreguidelines-pro-type-vararg
  int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);

  if (fd < 0) {
    return logical_size; // fallback
  }

  scope_exit close_fd([fd]() { ::close(fd); });

  if (::lseek(fd, 0, SEEK_SET) == -1) {
    return logical_size; // fallback
  }

  int whence = SEEK_DATA;
  off_t offset = 0;
  file_stat::off_type total_allocated{0};

  for (;;) {
    off_t rv = ::lseek(fd, offset, whence);

    if (rv < 0) {
      if (errno != ENXIO) {
        return logical_size; // fallback
      }
      break;
    }

    switch (whence) {
    case SEEK_DATA:
      whence = SEEK_HOLE;
      break;
    case SEEK_HOLE:
      if (rv > offset) {
        total_allocated += rv - offset;
      }
      whence = SEEK_DATA;
      break;
    default:
      DWARFS_PANIC("invalid whence");
    }

    offset = rv;
  }

  return total_allocated;
#else
  return logical_size; // fallback
#endif
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
  auto set_exception = [this, &path](std::string const& what) {
    exception_ = std::make_exception_ptr(std::system_error(
        ::GetLastError(), std::system_category(),
        fmt::format("{}: {}", what, path_to_utf8_string_sanitized(path))));
  };

  HANDLE h = ::CreateFileW(
      path.c_str(), GENERIC_READ,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
      nullptr);

  if (h == INVALID_HANDLE_VALUE) {
    set_exception("CreateFileW failed");
    return;
  }

  scope_exit close_handle([h]() { ::CloseHandle(h); });

  BY_HANDLE_FILE_INFORMATION bhfi{};
  FILE_BASIC_INFO basic{};
  FILE_STANDARD_INFO stdinfo{};

  if (!GetFileInformationByHandle(h, &bhfi)) {
    set_exception("GetFileInformationByHandle failed");
    return;
  }

  if (!GetFileInformationByHandleEx(h, FileBasicInfo, &basic, sizeof(basic))) {
    set_exception("GetFileInformationByHandleEx(FileBasicInfo) failed");
    return;
  }

  if (!GetFileInformationByHandleEx(h, FileStandardInfo, &stdinfo,
                                    sizeof(stdinfo))) {
    set_exception("GetFileInformationByHandleEx(FileStandardInfo) failed");
    return;
  }

  FILE_ATTRIBUTE_TAG_INFO tag{};
  bool const has_tag = GetFileInformationByHandleEx(h, FileAttributeTagInfo,
                                                    &tag, sizeof(tag)) != 0;
  bool const is_reparse =
      (basic.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
  bool const is_symlink = is_reparse && has_tag &&
                          (tag.ReparseTag == IO_REPARSE_TAG_SYMLINK ||
                           tag.ReparseTag == IO_REPARSE_TAG_MOUNT_POINT);
  bool const is_directory =
      (basic.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
  bool const is_readonly =
      (basic.FileAttributes & FILE_ATTRIBUTE_READONLY) != 0;

  valid_fields_ = file_stat::mode_valid | file_stat::nlink_valid |
                  file_stat::dev_valid | file_stat::ino_valid |
                  file_stat::uid_valid | file_stat::gid_valid |
                  file_stat::size_valid | file_stat::blocks_valid |
                  file_stat::atime_valid | file_stat::ctime_valid |
                  file_stat::mtime_valid | file_stat::allocated_size_valid;

  if (is_symlink) {
    mode_ = posix_file_type::symlink | 0777;
  } else if (is_directory) {
    mode_ = posix_file_type::directory | 0755;
  } else {
    mode_ = posix_file_type::regular;
    mode_ |= is_readonly ? 0444 : 0644;
    if (is_executable(path)) {
      mode_ |= 0111;
    }
  }

  nlink_ = stdinfo.NumberOfLinks;

  dev_ = bhfi.dwVolumeSerialNumber;
  ino_ = (static_cast<uint64_t>(bhfi.nFileIndexHigh) << 32) |
         static_cast<uint64_t>(bhfi.nFileIndexLow);

  // These don't make sense on Windows
  uid_ = 0;
  gid_ = 0;

  if (is_symlink) {
    size_ = 0;

    // Read reparse buffer to get the target path length.
    std::vector<uint8_t> buffer(MAXIMUM_REPARSE_DATA_BUFFER_SIZE);
    DWORD returned_len = 0;

    if (::DeviceIoControl(h, FSCTL_GET_REPARSE_POINT, nullptr, 0, buffer.data(),
                          static_cast<DWORD>(buffer.size()), &returned_len,
                          nullptr)) {
      auto const* reparse =
          reinterpret_cast<REPARSE_DATA_BUFFER*>(buffer.data());

      auto get_symlink_size =
          [total_size = reparse->ReparseDataLength](auto const& buffer) -> int {
        size_t const end = buffer.PrintNameOffset + buffer.PrintNameLength;
        if (end > total_size) {
          return 0;
        }
        auto const base = reinterpret_cast<BYTE const*>(buffer.PathBuffer);
        auto const w_print =
            reinterpret_cast<WCHAR const*>(base + buffer.PrintNameOffset);
        int const w_len_chars = buffer.PrintNameLength / sizeof(WCHAR);
        return utf8_len_of_print_name(w_print, w_len_chars);
      };

      if (reparse->ReparseTag == IO_REPARSE_TAG_SYMLINK) {
        size_ = get_symlink_size(reparse->SymbolicLinkReparseBuffer);
      } else if (reparse->ReparseTag == IO_REPARSE_TAG_MOUNT_POINT) {
        size_ = get_symlink_size(reparse->MountPointReparseBuffer);
      }
    }
  } else {
    size_ = stdinfo.EndOfFile.QuadPart; // logical size
  }

  allocated_size_ = size_;

  if (basic.FileAttributes & FILE_ATTRIBUTE_SPARSE_FILE) {
    allocated_size_ = get_sparse_file_allocated_size_win(h, size_);
  }

  blocks_ = (stdinfo.AllocationSize.QuadPart + 511) / 512;

  atimespec_ = to_unix_timespec(basic.LastAccessTime);
  mtimespec_ = to_unix_timespec(basic.LastWriteTime);
  ctimespec_ = to_unix_timespec(basic.ChangeTime);
}

#else

file_stat::file_stat(fs::path const& path) {
  struct ::stat st;

  if (::lstat(path.string().c_str(), &st) != 0) {
    exception_ = std::make_exception_ptr(std::system_error(
        errno, std::generic_category(),
        fmt::format("lstat: {}", path_to_utf8_string_sanitized(path))));
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
  if (S_ISREG(mode_) && st.st_blocks * 512 < size_) {
    allocated_size_ = get_sparse_file_allocated_size_posix(path, size_);
  } else {
    allocated_size_ = size_;
  }
  blksize_ = st.st_blksize;
  blocks_ = st.st_blocks;

  auto copy_timespec = [](file_stat::timespec_type& dst, auto const& src) {
    dst.sec = src.tv_sec;
    dst.nsec = src.tv_nsec;
  };

#ifdef __APPLE__
  copy_timespec(atimespec_, st.st_atimespec);
  copy_timespec(mtimespec_, st.st_mtimespec);
  copy_timespec(ctimespec_, st.st_ctimespec);
#else
  copy_timespec(atimespec_, st.st_atim);
  copy_timespec(mtimespec_, st.st_mtim);
  copy_timespec(ctimespec_, st.st_ctim);
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
  return atimespec_.sec;
}

void file_stat::set_atime(time_type atime) {
  valid_fields_ |= atime_valid;
  atimespec_.sec = atime;
  atimespec_.nsec = 0;
}

file_stat::time_type file_stat::mtime() const {
  ensure_valid(mtime_valid);
  return mtimespec_.sec;
}

void file_stat::set_mtime(time_type mtime) {
  valid_fields_ |= mtime_valid;
  mtimespec_.sec = mtime;
  mtimespec_.nsec = 0;
}

file_stat::time_type file_stat::ctime() const {
  ensure_valid(ctime_valid);
  return ctimespec_.sec;
}

void file_stat::set_ctime(time_type ctime) {
  valid_fields_ |= ctime_valid;
  ctimespec_.sec = ctime;
  ctimespec_.nsec = 0;
}

file_stat::timespec_type file_stat::atimespec() const {
  ensure_valid(atime_valid);
  return atimespec_;
}

void file_stat::set_atimespec(timespec_type atimespec) {
  valid_fields_ |= atime_valid;
  atimespec_ = atimespec;
}

void file_stat::set_atimespec(time_type sec, uint32_t nsec) {
  valid_fields_ |= atime_valid;
  atimespec_.sec = sec;
  atimespec_.nsec = nsec;
}

file_stat::timespec_type file_stat::mtimespec() const {
  ensure_valid(mtime_valid);
  return mtimespec_;
}

void file_stat::set_mtimespec(timespec_type mtimespec) {
  valid_fields_ |= mtime_valid;
  mtimespec_ = mtimespec;
}

void file_stat::set_mtimespec(time_type sec, uint32_t nsec) {
  valid_fields_ |= mtime_valid;
  mtimespec_.sec = sec;
  mtimespec_.nsec = nsec;
}

file_stat::timespec_type file_stat::ctimespec() const {
  ensure_valid(ctime_valid);
  return ctimespec_;
}

void file_stat::set_ctimespec(timespec_type ctimespec) {
  valid_fields_ |= ctime_valid;
  ctimespec_ = ctimespec;
}

void file_stat::set_ctimespec(time_type sec, uint32_t nsec) {
  valid_fields_ |= ctime_valid;
  ctimespec_.sec = sec;
  ctimespec_.nsec = nsec;
}

file_stat::off_type file_stat::allocated_size() const {
  ensure_valid(allocated_size_valid);
  return allocated_size_;
}

void file_stat::set_allocated_size(off_type allocated_size) {
  valid_fields_ |= allocated_size_valid;
  allocated_size_ = allocated_size;
}

std::ostream& operator<<(std::ostream& os, file_stat::timespec_type const& ts) {
  return os << "{" << ts.sec << ", " << ts.nsec << "}";
}

std::chrono::nanoseconds file_stat::native_time_resolution() {
#ifdef _WIN32
  return std::chrono::nanoseconds(100);
#else
  return std::chrono::nanoseconds(1);
#endif
}

} // namespace dwarfs
