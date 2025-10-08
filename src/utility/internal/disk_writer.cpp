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

#include <cassert>
#include <iostream>
#include <ranges>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#include <folly/portability/Windows.h>
#include <winioctl.h>
#else
#include <cerrno>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#include <dwarfs/file_range.h>
#include <dwarfs/scope_exit.h>

#include <dwarfs/utility/internal/disk_writer.h>

namespace dwarfs::utility::internal {

namespace fs = std::filesystem;
using namespace std::string_literals;

namespace {

#ifdef _WIN32

void make_node(fs::path const&, file_stat const&, std::error_code& ec) {
  ec = std::make_error_code(std::errc::not_supported);
}

void update_attributes(fs::path const& path, file_stat const& stat,
                       diagnostic_sink& ds) {
  // change owner and group: Windows has no concept of uid/gid

  // change permissions

  DWORD const attrs = ::GetFileAttributesW(path.c_str());

  if (attrs == INVALID_FILE_ATTRIBUTES) {
    ds.warning(path, "GetFileAttributes",
               std::error_code(::GetLastError(), std::system_category()));
  } else {
    DWORD new_attrs = attrs;

    if (stat.mode() & 0200) {
      new_attrs &= ~FILE_ATTRIBUTE_READONLY;
    } else {
      new_attrs |= FILE_ATTRIBUTE_READONLY;
    }

    if (new_attrs != attrs) {
      if (!::SetFileAttributesW(path.c_str(), new_attrs)) {
        ds.warning(path, "SetFileAttributes",
                   std::error_code(::GetLastError(), std::system_category()));
      }
    }
  }

  // change timestamps

  HANDLE h = ::CreateFileW(
      path.c_str(), GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS, nullptr);

  if (h == INVALID_HANDLE_VALUE) {
    ds.warning(path, "CreateFileW",
               std::error_code(::GetLastError(), std::system_category()));
  } else {
    scope_exit close_handle{[&]() {
      if (h != INVALID_HANDLE_VALUE) {
        if (!::CloseHandle(h)) {
          ds.warning(path, "CloseHandle",
                     std::error_code(::GetLastError(), std::system_category()));
        }
      }
    }};

    auto unix_time_to_filetime = [](file_stat::time_type t) {
      ULARGE_INTEGER li{};
      li.QuadPart = static_cast<ULONGLONG>(t) * 10000000ull +
                    116444736000000000ull; // seconds to 100-nanoseconds since
                                           // 1601-01-01
      FILETIME ft{};
      ft.dwLowDateTime = li.LowPart;
      ft.dwHighDateTime = li.HighPart;
      return ft;
    };

    auto const atime = unix_time_to_filetime(stat.atime());
    auto const mtime = unix_time_to_filetime(stat.mtime());

    if (!::SetFileTime(h, nullptr, &atime, &mtime)) {
      ds.warning(path, "SetFileTime",
                 std::error_code(::GetLastError(), std::system_category()));
    }
  }
}

class file_writer_ : public file_writer::impl {
 public:
  static file_writer
  create_temp(fs::path const& dir, diagnostic_sink& ds, std::error_code& ec) {
    ec.clear();

    auto const name = (dir / (L"dwarfs_file_writer_tmp_" +
                              std::to_wstring(::GetCurrentProcessId()) + L"_" +
                              std::to_wstring(::GetTickCount64()) + L".tmp"))
                          .wstring();

    HANDLE h = ::CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE,
                             FILE_SHARE_READ, nullptr, CREATE_NEW,
                             FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE |
                                 FILE_FLAG_OVERLAPPED,
                             nullptr);

    if (h == INVALID_HANDLE_VALUE) {
      ec = std::error_code(::GetLastError(), std::system_category());
      return {};
    }

    return file_writer{std::make_unique<file_writer_>(h, ds)};
  }

  static file_writer create(fs::path const& path, file_stat const& stat,
                            diagnostic_sink& ds, std::error_code& ec) {
    return create(path, &stat, ds, ec);
  }

  static file_writer
  create(fs::path const& path, diagnostic_sink& ds, std::error_code& ec) {
    return create(path, nullptr, ds, ec);
  }

  file_writer_(HANDLE h, diagnostic_sink& ds)
      : h_{h}
      , ds_{ds} {}

  file_writer_(HANDLE h, fs::path const& path, file_stat const& stat,
               diagnostic_sink& ds)
      : h_{h}
      , ds_{ds}
      , attrib_({path, stat}) {}

  ~file_writer_() override {
    std::error_code ec;

    commit(ec);

    if (ec) {
      std::cerr << "error closing file: " << ec.message() << "\n";
    }
  }

  void set_sparse(std::error_code& ec) override {
    assert(h_ != INVALID_HANDLE_VALUE);

    ec.clear();

    DWORD bytes = 0;

    if (!::DeviceIoControl(h_, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &bytes,
                           nullptr)) {
      ec = std::error_code(::GetLastError(), std::system_category());
    }
  }

  void truncate(file_size_t size, std::error_code& ec) override {
    assert(h_ != INVALID_HANDLE_VALUE);

    ec.clear();

    LARGE_INTEGER li{};
    li.QuadPart = static_cast<LONGLONG>(size);

    if (!::SetFilePointerEx(h_, li, nullptr, FILE_BEGIN)) {
      ec = std::error_code(::GetLastError(), std::system_category());
      return;
    }

    if (!::SetEndOfFile(h_)) {
      ec = std::error_code(::GetLastError(), std::system_category());
    }
  }

  void write_data(file_off_t offset, void const* buffer, size_t count,
                  std::error_code& ec) override {
    assert(h_ != INVALID_HANDLE_VALUE);

    ec.clear();

    auto p = reinterpret_cast<uint8_t const*>(buffer);
    auto off64 = static_cast<uint64_t>(offset);

    while (count > 0) {
      DWORD const to_write = count > static_cast<std::uint64_t>(MAXDWORD)
                                 ? MAXDWORD
                                 : static_cast<DWORD>(count);

      OVERLAPPED ov{};
      ov.Offset = static_cast<DWORD>(off64 & 0xFFFFFFFFull);
      ov.OffsetHigh = static_cast<DWORD>((off64 >> 32) & 0xFFFFFFFFull);

      DWORD wrote = 0;

      if (!::WriteFile(h_, p, to_write, &wrote, &ov)) {
        auto const err = ::GetLastError();

        if (err != ERROR_IO_PENDING) {
          ec = std::error_code(err, std::system_category());
          return;
        }

        if (!::GetOverlappedResult(h_, &ov, &wrote, TRUE)) {
          ec = std::error_code(::GetLastError(), std::system_category());
          return;
        }
      }

      // if nothing was written, avoid infinite loop
      if (wrote == 0) {
        ec = std::make_error_code(std::errc::io_error);
        return;
      }

      p += wrote;
      count -= wrote;
      off64 += wrote;
    }
  }

  void write_hole(file_off_t offset, file_size_t length,
                  std::error_code& ec) override {
    assert(h_ != INVALID_HANDLE_VALUE);

    ec.clear();

    FILE_ZERO_DATA_INFORMATION info{};
    info.FileOffset.QuadPart = static_cast<LONGLONG>(offset);
    info.BeyondFinalZero.QuadPart = static_cast<LONGLONG>(offset + length);

    DWORD bytes = 0;

    if (!::DeviceIoControl(h_, FSCTL_SET_ZERO_DATA, &info, sizeof(info),
                           nullptr, 0, &bytes, nullptr)) {
      ds_.warning(attrib_ ? attrib_->first : fs::path{}, "FSCTL_SET_ZERO_DATA",
                  std::error_code(::GetLastError(), std::system_category()));
    }
  }

  void commit(std::error_code& ec) override {
    ec.clear();

    if (h_ != INVALID_HANDLE_VALUE) {
      if (!::CloseHandle(h_)) {
        ec = std::error_code(::GetLastError(), std::system_category());
        return;
      }

      h_ = INVALID_HANDLE_VALUE;
    }

    if (attrib_) {
      update_attributes(attrib_->first, attrib_->second, ds_);
    }
  }

  std::any get_native_handle() const override {
    assert(h_ != INVALID_HANDLE_VALUE);

    return h_;
  }

 private:
  static file_writer create(fs::path const& path, file_stat const* stat,
                            diagnostic_sink& ds, std::error_code& ec) {
    ec.clear();

    HANDLE h = ::CreateFileW(
        path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);

    if (h == INVALID_HANDLE_VALUE) {
      ec = std::error_code(::GetLastError(), std::system_category());
      return {};
    }

    if (stat) {
      return file_writer{std::make_unique<file_writer_>(h, path, *stat, ds)};
    }

    return file_writer{std::make_unique<file_writer_>(h, ds)};
  }

  HANDLE h_{INVALID_HANDLE_VALUE};
  diagnostic_sink& ds_;
  std::optional<std::pair<fs::path, file_stat>> attrib_;
};

#else

void make_node(fs::path const& path, file_stat const& stat,
               std::error_code& ec) {
  if (::mknod(path.c_str(), stat.mode(), static_cast<dev_t>(stat.rdev())) ==
      -1) {
    ec = std::error_code{errno, std::generic_category()};
  }
}

void update_attributes(fs::path const& path, file_stat const& stat,
                       diagnostic_sink& ds) {
  static constexpr bool kSupportsSymlinkChmod{
#if defined(__FreeBSD__) || defined(__APPLE__)
      true
#else
      false
#endif
  };

  int const flags = stat.is_symlink() ? AT_SYMLINK_NOFOLLOW : 0;

  // change owner and group

  if (::fchownat(AT_FDCWD, path.c_str(), stat.uid(), stat.gid(), flags) == -1) {
    ds.warning(path, "fchownat",
               std::error_code{errno, std::generic_category()});
  }

  // change permissions

  if (kSupportsSymlinkChmod || !stat.is_symlink()) {
    if (::fchmodat(AT_FDCWD, path.c_str(), stat.mode(), flags) == -1) {
      ds.warning(path, "fchmodat",
                 std::error_code{errno, std::generic_category()});
    }
  }

  // change timestamps

  struct timespec ts[2];
  ts[0].tv_sec = stat.atime();
  ts[0].tv_nsec = 0;
  ts[1].tv_sec = stat.mtime();
  ts[1].tv_nsec = 0;

  if (::utimensat(AT_FDCWD, path.c_str(), ts, flags) == -1) {
    ds.warning(path, "utimensat",
               std::error_code{errno, std::generic_category()});
  }
}

class file_writer_ : public file_writer::impl {
 public:
  static file_writer
  create_temp(fs::path const& dir, diagnostic_sink& ds, std::error_code& ec) {
    ec.clear();

    auto tmpfile = (dir / "dwarfs_file_writer_XXXXXX\0"s).string();
    auto fd = ::mkstemp(tmpfile.data());

    if (fd < 0) {
      ec = std::error_code(errno, std::generic_category());
      return {};
    }

    ::unlink(tmpfile.c_str()); // best effort cleanup

    return file_writer{std::make_unique<file_writer_>(fd, ds)};
  }

  static file_writer create(fs::path const& path, file_stat const& stat,
                            diagnostic_sink& ds, std::error_code& ec) {
    return create(path, &stat, ds, ec);
  }

  static file_writer
  create(fs::path const& path, diagnostic_sink& ds, std::error_code& ec) {
    return create(path, nullptr, ds, ec);
  }

  file_writer_(int fd, diagnostic_sink& ds)
      : fd_{fd}
      , ds_{ds} {}

  file_writer_(int fd, fs::path const& path, file_stat const& stat,
               diagnostic_sink& ds)
      : fd_{fd}
      , ds_{ds}
      , attrib_({path, stat}) {}

  ~file_writer_() override {
    std::error_code ec;

    commit(ec);

    if (ec) {
      std::cerr << "error closing file: " << ec.message() << "\n";
    }
  }

  void set_sparse(std::error_code& ec) override { ec.clear(); }

  void truncate(file_size_t size, std::error_code& ec) override {
    assert(fd_ >= 0);

    ec.clear();

    if (::ftruncate(fd_, static_cast<off_t>(size)) != 0) {
      ec = std::error_code(errno, std::generic_category());
    }
  }

  void write_data(file_off_t offset, void const* buffer, size_t count,
                  std::error_code& ec) override {
    assert(fd_ >= 0);

    ec.clear();

    auto p = reinterpret_cast<uint8_t const*>(buffer);
    auto off = static_cast<off_t>(offset);

    while (count > 0) {
      auto n = ::pwrite(fd_, p, count, off);

      if (n < 0) {
        if (errno == EINTR) {
          continue;
        }

        ec = std::error_code(errno, std::generic_category());

        return;
      }

      off += static_cast<off_t>(n);
      p += static_cast<size_t>(n);
      count -= static_cast<size_t>(n);
    }
  }

  void write_hole(file_off_t offset [[maybe_unused]],
                  file_size_t length [[maybe_unused]],
                  std::error_code& ec) override {
    assert(fd_ >= 0);

    ec.clear();
#ifdef F_PUNCHHOLE
    holes_.emplace_back(file_range{offset, length});
#endif
  }

  void commit(std::error_code& ec) override {
    ec.clear();

    if (fd_ != -1) {
#ifdef F_PUNCHHOLE
      for (auto const& hole : holes_) {
        punch_hole(fd_, hole.offset(), hole.size(), ec);

        if (ec) {
          ds_.warning(attrib_ ? attrib_->first : fs::path{}, "punch_hole", ec);
        }
      }

      holes_.clear();
#endif

      if (::close(fd_) == -1 && !ec) {
        ec = std::error_code{errno, std::generic_category()};
        return;
      }

      fd_ = -1;

      if (attrib_) {
        update_attributes(attrib_->first, attrib_->second, ds_);
      }
    }
  }

  std::any get_native_handle() const override {
    assert(fd_ >= 0);

    return fd_;
  }

 private:
  static file_writer create(fs::path const& path, file_stat const* stat,
                            diagnostic_sink& ds, std::error_code& ec) {
    ec.clear();

    auto fd = ::open(path.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (fd < 0) {
      ec = std::error_code(errno, std::generic_category());
      return {};
    }

    if (stat) {
      return file_writer{std::make_unique<file_writer_>(fd, path, *stat, ds)};
    }

    return file_writer{std::make_unique<file_writer_>(fd, ds)};
  }

#ifdef F_PUNCHHOLE
  static void
  punch_hole(int fd, file_off_t off, file_off_t len, std::error_code& ec) {
    ec.clear();

    assert(fd >= 0);

    if (len > 0) {
      fpunchhole_t ph{};

      ph.fp_flags = 0; // currently unused
      ph.reserved = 0;
      ph.fp_offset = off; // start of region to deallocate
      ph.fp_length = len; // size of region

      if (::fcntl(fd, F_PUNCHHOLE, &ph) == -1) {
        ec = std::error_code(errno, std::generic_category());
      }
    }
  }
#endif

  int fd_{-1};
  diagnostic_sink& ds_;
  std::optional<std::pair<fs::path, file_stat>> attrib_;
#ifdef F_PUNCHHOLE
  std::vector<file_range> holes_;
#endif
};

#endif

struct pending_attrib {
  fs::path path;
  file_stat stat;
};

class disk_writer_ : public disk_writer::impl {
 public:
  disk_writer_(fs::path const& base, diagnostic_sink& ds)
      : base_{base}
      , ds_{ds} {}

  ~disk_writer_() override {
    std::error_code ec;
    commit(ec);

    if (ec) {
      std::cerr << "error finalizing disk writer: " << ec.message() << "\n";
    }
  }

  void create_entry(fs::path const& path, file_stat const& stat,
                    std::error_code& ec) override {
    ec.clear();

    auto const full_path = base_ / path;
    auto const type = stat.type();

    switch (type) {
    case posix_file_type::symlink:
    case posix_file_type::regular:
      // these should be created via create_symlink() and create_file()
      ec = std::make_error_code(std::errc::invalid_argument);
      break;

    case posix_file_type::block:
    case posix_file_type::character:
    case posix_file_type::fifo:
    case posix_file_type::socket:
      make_node(full_path, stat, ec);
      break;

    case posix_file_type::directory:
      if (fs::exists(full_path, ec) && !ec) {
        if (fs::is_directory(full_path, ec) && !ec) {
          break;
        }

        fs::remove(full_path, ec);
      }

      if (ec) {
        return;
      }

#ifdef _WIN32
      fs::create_directory(full_path, ec);
#else
      if (::mkdir(full_path.c_str(), 0700) == -1) {
        ec = std::error_code{errno, std::generic_category()};
      }
#endif
      break;
    }

    if (!ec) {
      if (type == posix_file_type::directory) {
        pending_attribs_.push_back({path, stat});
      } else {
        update_attributes(full_path, stat, ds_);
      }
    }
  }

  void create_symlink(fs::path const& path, file_stat const& stat,
                      fs::path const& target, std::error_code& ec) override {
    ec.clear();

    auto const full_path = base_ / path;

    if (!remove_if_exists(full_path, ec)) {
      return;
    }

#ifdef _WIN32
    auto const full_target = target.is_absolute() ? target : base_ / target;

    if (fs::exists(full_target) && fs::is_directory(full_target)) {
      fs::create_directory_symlink(target, full_path, ec);
    } else {
      fs::create_symlink(target, full_path, ec);
    }
#else
    fs::create_symlink(target, full_path, ec);
#endif

    if (!ec) {
      update_attributes(full_path, stat, ds_);
    }
  }

  std::optional<file_writer>
  create_file(fs::path const& path, file_stat const& stat,
              std::error_code& ec) override {
    ec.clear();

    auto const full_path = base_ / path;

    if (stat.nlink() > 1) {
      auto const [it, inserted] = hardlink_map_.emplace(stat.ino(), full_path);

      if (!inserted) {
        if (remove_if_exists(full_path, ec)) {
          fs::create_hard_link(it->second, full_path, ec);
        }

        return std::nullopt;
      }
    }

    return file_writer_::create(full_path, stat, ds_, ec);
  }

  void commit(std::error_code& ec) override {
    ec.clear();

    for (auto const& [path, stat] :
         std::ranges::reverse_view(pending_attribs_)) {
      update_attributes(base_ / path, stat, ds_);
    }

    pending_attribs_.clear();
    hardlink_map_.clear();
  }

 private:
  bool remove_if_exists(fs::path const& p, std::error_code& ec) {
    if (fs::exists(p, ec)) {
      if (!ec) {
        fs::remove(p, ec);
      }

      if (ec) {
        return false;
      }
    }

    return true;
  }

  fs::path base_;
  diagnostic_sink& ds_;
  std::unordered_map<file_stat::ino_type, fs::path> hardlink_map_;
  std::vector<pending_attrib> pending_attribs_;
};

} // namespace

file_writer
file_writer::create_native(fs::path const& path, diagnostic_sink& ds,
                           std::error_code& ec) {
  return file_writer_::create(path, ds, ec);
}

file_writer
file_writer::create_native_temp(fs::path const& dir, diagnostic_sink& ds,
                                std::error_code& ec) {
  return file_writer_::create_temp(dir, ds, ec);
}

disk_writer
disk_writer::create_native(fs::path const& base, diagnostic_sink& ds) {
  return disk_writer{std::make_unique<disk_writer_>(base, ds)};
}

} // namespace dwarfs::utility::internal
