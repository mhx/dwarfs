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
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>

#ifndef _WIN32
#include <dirent.h>
#include <pthread.h>
#include <sys/types.h>
#endif

#include <dwarfs/portability/unistd.h>

#ifdef __FreeBSD__
#include <pthread_np.h>
#endif

#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/thread_info.h>
#endif

#include <dwarfs/os_access_generic.h>
#include <dwarfs/string.h>
#include <dwarfs/util.h>

#include <dwarfs/internal/io_ops.h>
#include <dwarfs/internal/mappable_file.h>
#include <dwarfs/internal/mmap_file_view.h>
#include <dwarfs/internal/os_access_generic_data.h>
#include <dwarfs/internal/read_file_view.h>
#include <dwarfs/internal/thread_util.h>

namespace dwarfs {

namespace fs = std::filesystem;

namespace {

#ifdef _WIN32
constexpr inline bool kIsWindows = true;
constexpr inline char kPathListSep = ';';
constexpr inline int kExecOk = 0;
#else
constexpr inline bool kIsWindows = false;
constexpr inline char kPathListSep = ':';
constexpr inline int kExecOk = X_OK;
#endif

#ifdef _WIN32

class generic_dir_reader final : public dir_reader {
 public:
  explicit generic_dir_reader(fs::path const& path)
      : it_(fs::directory_iterator(path)) {}

  bool read(fs::path& name) override {
    if (it_ != std::default_sentinel) {
      name.assign(it_->path());
      ++it_;
      return true;
    }

    return false;
  }

 private:
  fs::directory_iterator it_;
};

#else

class posix_dir_reader final : public dir_reader {
 public:
  explicit posix_dir_reader(fs::path const& path)
      : parent_{path}
      , dir_{::opendir(path.c_str())} {
    if (!dir_) {
      throw std::system_error(errno, std::generic_category(),
                              "opendir: " + path.string());
    }
  }

  bool read(fs::path& name) override {
    errno = 0;

    while (auto* ent = ::readdir(dir_.get())) {
      std::string_view leaf{&ent->d_name[0]};

      if (leaf == "." || leaf == "..") {
        continue;
      }

      name = parent_;
      name /= leaf;
      return true;
    }

    if (errno != 0) {
      throw std::system_error(errno, std::generic_category(), "readdir");
    }

    return false;
  }

 private:
  struct closer {
    void operator()(DIR* d) const noexcept {
      if (d) {
        ::closedir(d);
      }
    }
  };

  fs::path parent_;
  std::unique_ptr<DIR, closer> dir_;
};

#endif

} // namespace

std::unique_ptr<dir_reader>
os_access_generic::opendir(fs::path const& path) const {
#ifdef _WIN32
  return std::make_unique<generic_dir_reader>(path);
#else
  return std::make_unique<posix_dir_reader>(path);
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

fs::path os_access_generic::find_executable(fs::path const& name) const {
  DWARFS_PUSH_WARNING
  DWARFS_GCC14_DISABLE_WARNING("-Wnrvo")

  auto is_executable = [this](fs::path const& p) {
    std::error_code ec;
    // access() succeeds for directories (X means "searchable" there),
    // so the file type has to be checked separately.
    return fs::is_regular_file(p, ec) && access(p, kExecOk) == 0;
  };

  // on Windows, PATHEXT is a semicolon-separated list of extensions to try
  // when searching for executables
  auto const extensions = [this]() -> std::vector<std::string> {
    if constexpr (kIsWindows) {
      if (auto pathext = getenv("PATHEXT")) {
        auto exts = split_to<std::vector<std::string>>(*pathext, kPathListSep);
        std::erase_if(exts, [](auto const& e) { return e.empty(); });
        if (!exts.empty()) {
          return exts;
        }
      }
      return {".COM", ".EXE", ".BAT", ".CMD"};
    } else {
      return {};
    }
  }();

  auto try_base = [&](fs::path const& base) {
    // an explicit extension is honoured as given, so e.g. "python3.11" still
    // resolves even though ".11" is not in PATHEXT
    if (!kIsWindows || base.has_extension()) {
      if (is_executable(base)) {
        return base;
      }
    }

    for (auto const& ext : extensions) {
      auto candidate = base;
      candidate += ext;
      if (is_executable(candidate)) {
        return candidate;
      }
    }

    return fs::path{};
  };

  // name carrying a directory component is used as-is
  if (name.has_parent_path()) {
    return try_base(name);
  }

  auto path_env = getenv("PATH");

  if (!path_env) {
    return {};
  }

  for (auto const& dir :
       split_to<std::vector<std::string>>(*path_env, kPathListSep)) {
    if (dir.empty()) {
      continue;
    }

    if (auto found = try_base(fs::path{dir} / name); !found.empty()) {
      return found;
    }
  }

  return {};

  DWARFS_POP_WARNING
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
