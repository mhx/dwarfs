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
#include <cstdlib>
#include <iostream>

#ifndef _WIN32
#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/types.h>
#endif

#include <dwarfs/portability/unistd.h>

#if __has_include(<boost/process/v2/environment.hpp>) && defined(DWARFS_HAVE_CLOSE_RANGE)
#define BOOST_PROCESS_VERSION 2
#include <boost/process/v2/environment.hpp>
#elif __has_include(<boost/process/v1/search_path.hpp>)
#define BOOST_PROCESS_VERSION 1
#include <boost/process/v1/search_path.hpp>
#else
#include <boost/process/search_path.hpp>
#endif

#ifdef __FreeBSD__
#include <pthread_np.h>
#endif

#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/thread_info.h>
#endif

#include <dwarfs/error.h>
#include <dwarfs/os_access_generic.h>
#include <dwarfs/util.h>

#include <dwarfs/internal/io_ops.h>
#include <dwarfs/internal/mappable_file.h>
#include <dwarfs/internal/mmap_file_view.h>
#include <dwarfs/internal/os_access_generic_data.h>
#include <dwarfs/internal/read_file_view.h>
#include <dwarfs/internal/thread_util.h>

#if defined(_DIRENT_HAVE_D_TYPE) || defined(__linux__) ||                      \
    defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) ||       \
    defined(__OpenBSD__)
#define DWARFS_HAVE_DIRENT_D_TYPE 1
#endif

namespace dwarfs {

namespace fs = std::filesystem;

namespace {

#ifdef _WIN32

class generic_dir_descriptor final : public dir_descriptor::impl {
 public:
  generic_dir_descriptor(os_access const& os, fs::path const& path)
      : os_{&os}
      , path_{path} {}

  file_stat symlink_info(std::filesystem::path const& relpath) const override {
    return os_->symlink_info(path_ / relpath);
  }

  std::filesystem::path
  read_symlink(std::filesystem::path const& relpath) const override {
    return os_->read_symlink(path_ / relpath);
  }

  file_view open_file(std::filesystem::path const& relpath) const override {
    return os_->open_file(path_ / relpath);
  }

 private:
  os_access const* os_{nullptr};
  fs::path path_;
};

class generic_dir_reader final : public dir_reader {
 public:
  generic_dir_reader(os_access const& os, fs::path const& path)
      : os_{&os}
      , dir_path_{path}
      , it_{fs::directory_iterator(path)} {}

  bool read(dir_entry& entry) override {
    if (it_ != std::default_sentinel) {
      // TODO: only fill in the leaf name
      entry.name.assign(it_->path());
      // TODO: maybe we can optimize this for Windows...
      entry.stat_hint.reset();
      entry.stat_hint.emplace(it_->path());
      entry.type = entry.stat_hint->type();
      ++it_;
      return true;
    }

    return false;
  }

  dir_descriptor descriptor() const override {
    return dir_descriptor(
        std::make_shared<generic_dir_descriptor>(*os_, dir_path_));
  }

 private:
  os_access const* os_{nullptr};
  fs::path dir_path_;
  fs::directory_iterator it_;
};

#else

class posix_dir_descriptor final : public dir_descriptor::impl {
 public:
  posix_dir_descriptor(int fd, internal::os_access_generic_data const& data)
      : fd_{dup_dir_fd(fd)}
      , data_{&data} {}

  ~posix_dir_descriptor() {
    if (fd_ != -1) {
      ::close(fd_);
    }
  }

  file_stat symlink_info(std::filesystem::path const& relpath) const override {
    return {fd_, relpath};
  }

  std::filesystem::path
  read_symlink(std::filesystem::path const& relpath) const override {
    std::string buf(256, '\0');

    for (;;) {
      ssize_t nread;

      do {
        nread = ::readlinkat(fd_, relpath.c_str(), buf.data(), buf.size());
      } while (nread < 0 && errno == EINTR);

      if (nread < 0) {
        auto const err = errno;
        throw std::system_error(err, std::generic_category(),
                                "readlinkat: " + relpath.string());
      }

      if (static_cast<size_t>(nread) < buf.size()) {
        buf.resize(static_cast<size_t>(nread));
        break;
      }

      // grow and retry
      buf.resize(buf.size() * 2);
    }

    return fs::path(std::move(buf));
  }

  file_view open_file(std::filesystem::path const& relpath) const override {
    auto const& ops = data_->mm_ops();

    if (data_->open_mode() == internal::open_file_mode::mmap) {
      return internal::create_mmap_file_view(ops, fd_, relpath,
                                             data_->fv_opts());
    }

    return internal::create_read_file_view(ops, fd_, relpath);
  }

 private:
  static int dup_dir_fd(int fd) {
    int dup_fd;

#ifdef F_DUPFD_CLOEXEC
    dup_fd = ::fcntl(fd, F_DUPFD_CLOEXEC, 3);
#else
    dup_fd = ::fcntl(fd, F_DUPFD, 3);
#endif

    if (dup_fd == -1) {
      throw std::system_error(errno, std::generic_category(), "dup dirfd");
    }

#ifndef F_DUPFD_CLOEXEC
    int flags = ::fcntl(dup_fd, F_GETFD);
    if (flags != -1) {
      ::fcntl(dup_fd, F_SETFD, flags | FD_CLOEXEC);
    }
#endif

    return dup_fd;
  }

  int fd_{-1};
  internal::os_access_generic_data const* data_{nullptr};
};

class posix_dir_reader final : public dir_reader {
 public:
  posix_dir_reader(fs::path const& path,
                   internal::os_access_generic_data const& data)
      : parent_{path}
      , dir_{checked_opendir(path)}
      , descriptor_{make_descriptor(dir_.get(), data)} {}

  bool read(dir_entry& entry) override {
    errno = 0;

    while (auto* ent = ::readdir(dir_.get())) {
      std::string_view leaf{&ent->d_name[0]};

      if (leaf == "." || leaf == "..") {
        continue;
      }

      // TODO: only fill in the leaf name
      entry.name = parent_;
      entry.name /= leaf;
      entry.stat_hint.reset();

      std::optional<posix_file_type::value> type;

#ifdef DWARFS_HAVE_DIRENT_D_TYPE
      switch (ent->d_type) {
      case DT_BLK:
        type = posix_file_type::block;
        break;
      case DT_CHR:
        type = posix_file_type::character;
        break;
      case DT_DIR:
        type = posix_file_type::directory;
        break;
      case DT_FIFO:
        type = posix_file_type::fifo;
        break;
      case DT_LNK:
        type = posix_file_type::symlink;
        break;
      case DT_REG:
        type = posix_file_type::regular;
        break;
      case DT_SOCK:
        type = posix_file_type::socket;
        break;
      case DT_UNKNOWN:
        // filled below
        break;
      default:
        DWARFS_PANIC("unsupported d_type value: " +
                     std::to_string(ent->d_type));
      }
#endif

      if (type.has_value()) {
        entry.type = *type;
      } else {
        entry.stat_hint.emplace(entry.name);
        entry.type = entry.stat_hint->type();
      }

      return true;
    }

    if (errno != 0) {
      throw std::system_error(errno, std::generic_category(), "readdir");
    }

    return false;
  }

  dir_descriptor descriptor() const override { return descriptor_; }

 private:
  static DIR* checked_opendir(fs::path const& path) {
    auto* dir = ::opendir(path.c_str());
    if (!dir) {
      auto const err = errno;
      throw std::system_error(err, std::generic_category(),
                              "opendir: " + path.string());
    }
    return dir;
  }

  static std::shared_ptr<dir_descriptor::impl>
  make_descriptor(DIR* dir, internal::os_access_generic_data const& data) {
    int const fd = ::dirfd(dir);

    if (fd == -1) {
      throw std::system_error(errno, std::generic_category(), "dirfd");
    }

    return std::make_shared<posix_dir_descriptor>(fd, data);
  }

  struct closer {
    void operator()(DIR* d) const noexcept {
      if (d) {
        ::closedir(d);
      }
    }
  };

  fs::path parent_;
  std::unique_ptr<DIR, closer> dir_;
  dir_descriptor descriptor_;
};

#endif

} // namespace

dir_descriptor::dir_descriptor(std::shared_ptr<impl> dd)
    : impl_{std::move(dd)} {}

file_stat dir_descriptor::symlink_info(fs::path const& relpath) const {
  DWARFS_CHECK(impl_, "invalid directory descriptor");
  DWARFS_CHECK(relpath.is_relative(), "relative path expected");
  return impl_->symlink_info(relpath);
}

fs::path dir_descriptor::read_symlink(fs::path const& relpath) const {
  DWARFS_CHECK(impl_, "invalid directory descriptor");
  DWARFS_CHECK(relpath.is_relative(), "relative path expected");
  return impl_->read_symlink(relpath);
}

file_view dir_descriptor::open_file(fs::path const& relpath) const {
  DWARFS_CHECK(impl_, "invalid directory descriptor");
  DWARFS_CHECK(relpath.is_relative(), "relative path expected");
  return impl_->open_file(relpath);
}

std::unique_ptr<dir_reader>
os_access_generic::opendir(fs::path const& path) const {
#ifdef _WIN32
  return std::make_unique<generic_dir_reader>(*this, path);
#else
  return std::make_unique<posix_dir_reader>(path, *data_);
#endif
}

file_stat os_access_generic::symlink_info(fs::path const& path) const {
  return file_stat(path);
}

fs::path os_access_generic::read_symlink(fs::path const& path) const {
  return fs::read_symlink(path);
}

file_view os_access_generic::open_file(fs::path const& path) const {
  if (data_->open_mode() == internal::open_file_mode::mmap) {
    return internal::create_mmap_file_view(data_->mm_ops(), path,
                                           data_->fv_opts());
  }

  return internal::create_read_file_view(data_->mm_ops(), path);
}

readonly_memory_mapping
os_access_generic::map_empty_readonly(size_t size) const {
  return internal::mappable_file::map_empty_readonly(data_->mm_ops(), size);
}

memory_mapping os_access_generic::map_empty(size_t size) const {
  return internal::mappable_file::map_empty(data_->mm_ops(), size);
}

int os_access_generic::access(fs::path const& path, int mode) const {
#ifdef _WIN32
  return ::_waccess(path.c_str(), mode);
#else
  return ::access(path.c_str(), mode);
#endif
}

fs::path os_access_generic::canonical(fs::path const& path) const {
  return canonical_path(path);
}

fs::path os_access_generic::current_path() const { return fs::current_path(); }

std::optional<std::string>
os_access_generic::getenv(std::string_view name) const {
  std::string name_str(name);
  if (auto value = std::getenv(name_str.c_str())) {
    return value;
  }
  return std::nullopt;
}

void os_access_generic::thread_set_affinity(std::thread::id tid
                                            [[maybe_unused]],
                                            std::span<int const> cpus
                                            [[maybe_unused]],
                                            std::error_code& ec
                                            [[maybe_unused]]) const {
#if !(defined(_WIN32) || defined(__APPLE__))
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);

  for (auto cpu : cpus) {
    CPU_SET(cpu, &cpuset);
  }

  if (auto error = pthread_setaffinity_np(internal::std_to_pthread_id(tid),
                                          sizeof(cpu_set_t), &cpuset);
      error != 0) {
    ec.assign(error, std::generic_category());
  }
#endif
}

std::chrono::nanoseconds
os_access_generic::thread_get_cpu_time(std::thread::id tid,
                                       std::error_code& ec) const {
#ifdef _WIN32

  HANDLE h = ::OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE,
                          internal::std_to_win_thread_id(tid));

  if (h == nullptr) {
    ec.assign(::GetLastError(), std::system_category());
    return {};
  }

  FILETIME t_create, t_exit, t_sys, t_user;

  if (!::GetThreadTimes(h, &t_create, &t_exit, &t_sys, &t_user)) {
    ec.assign(::GetLastError(), std::system_category());
    return {};
  }

  uint64_t sys =
      (static_cast<uint64_t>(t_sys.dwHighDateTime) << 32) + t_sys.dwLowDateTime;
  uint64_t user = (static_cast<uint64_t>(t_user.dwHighDateTime) << 32) +
                  t_user.dwLowDateTime;

  return std::chrono::nanoseconds(100 * (sys + user));

#elif defined(__APPLE__)

  auto port = ::pthread_mach_thread_np(internal::std_to_pthread_id(tid));

  ::thread_basic_info_data_t ti;
  ::mach_msg_type_number_t count = THREAD_BASIC_INFO_COUNT;

  if (::thread_info(port, THREAD_BASIC_INFO,
                    reinterpret_cast<thread_info_t>(&ti),
                    &count) != KERN_SUCCESS) {
    ec = std::make_error_code(std::errc::not_supported);
    return {};
  }

  return std::chrono::seconds(ti.user_time.seconds + ti.system_time.seconds) +
         std::chrono::microseconds(ti.user_time.microseconds +
                                   ti.system_time.microseconds);

#else

  ::clockid_t cid;
  struct ::timespec ts;

  if (auto err =
          ::pthread_getcpuclockid(internal::std_to_pthread_id(tid), &cid);
      err != 0) {
    ec.assign(err, std::generic_category());
    return {};
  }

  if (::clock_gettime(cid, &ts) != 0) {
    ec.assign(errno, std::generic_category());
    return {};
  }

  return std::chrono::seconds(ts.tv_sec) + std::chrono::nanoseconds(ts.tv_nsec);

#endif
}

std::filesystem::path
os_access_generic::find_executable(std::filesystem::path const& name) const {
#if BOOST_PROCESS_VERSION == 2
  return boost::process::v2::environment::find_executable(name.wstring())
      .wstring();
#else
  return boost::process::search_path(name.wstring()).wstring();
#endif
}

std::chrono::nanoseconds
os_access_generic::native_file_time_resolution() const {
  return file_stat::native_time_resolution();
}

os_access_generic::os_access_generic()
    : os_access_generic(std::cerr) {}

os_access_generic::os_access_generic(std::ostream& err)
    : data_{std::make_unique<internal::os_access_generic_data>(err,
                                                               std::getenv)} {}

os_access_generic::~os_access_generic() = default;

} // namespace dwarfs
