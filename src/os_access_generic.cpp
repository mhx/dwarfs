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
#include <cstdlib>

#include <folly/portability/PThread.h>
#include <folly/portability/Unistd.h>

#include <boost/process/search_path.hpp>

#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/thread_info.h>
#endif

#include <dwarfs/mmap.h>
#include <dwarfs/os_access_generic.h>
#include <dwarfs/performance_monitor.h>
#include <dwarfs/util.h>

namespace dwarfs {

namespace fs = std::filesystem;

namespace {

#ifdef _WIN32

DWORD std_to_win_thread_id(std::thread::id tid) {
  static_assert(sizeof(std::thread::id) == sizeof(DWORD),
                "Win32 thread id type mismatch");
  DWORD id;
  std::memcpy(&id, &tid, sizeof(id));
  return id;
}

#else

pthread_t std_to_pthread_id(std::thread::id tid) {
  static_assert(std::is_same_v<pthread_t, std::thread::native_handle_type>);
  static_assert(sizeof(std::thread::id) ==
                sizeof(std::thread::native_handle_type));
  pthread_t id{0};
  std::memcpy(&id, &tid, sizeof(id));
  return id;
}

#endif

class generic_dir_reader final : public dir_reader {
 public:
  explicit generic_dir_reader(fs::path const& path)
      : it_(fs::directory_iterator(path)) {}

  bool read(fs::path& name) override {
    if (it_ != fs::directory_iterator()) {
      name.assign(it_->path());
      ++it_;
      return true;
    }

    return false;
  }

 private:
  fs::directory_iterator it_;
};

} // namespace

class os_access_generic::state {
 public:
  state(std::shared_ptr<performance_monitor const> perfmon)
      // clang-format off
      PERFMON_CLS_PROXY_COLON_INIT(perfmon, "os_access_generic")
      PERFMON_CLS_TIMER_INIT(map_file, "map_file")
      // clang-format on
      {}

  std::unique_ptr<mmif> map_file(fs::path const& path) const {
    PERFMON_CLS_SCOPED_SECTION(map_file);
    return std::make_unique<mmap>(path);
  }

  std::unique_ptr<mmif>
  map_file(fs::path const& path, size_t size) const {
    PERFMON_CLS_SCOPED_SECTION(map_file);
    return std::make_unique<mmap>(path, size);
  }

 private:
  PERFMON_CLS_PROXY_DECL
  PERFMON_CLS_TIMER_DECL(map_file)
};

os_access_generic::os_access_generic() = default;
os_access_generic::~os_access_generic() = default;

void os_access_generic::set_perfmon(
    std::shared_ptr<performance_monitor const> perfmon) const {
  state_ = std::make_unique<state>(std::move(perfmon));
}

std::unique_ptr<dir_reader>
os_access_generic::opendir(fs::path const& path) const {
  return std::make_unique<generic_dir_reader>(path);
}

file_stat os_access_generic::symlink_info(fs::path const& path) const {
  return file_stat(path);
}

fs::path os_access_generic::read_symlink(fs::path const& path) const {
  return fs::read_symlink(path);
}

std::unique_ptr<mmif> os_access_generic::map_file(fs::path const& path) const {
  return state_ ? state_->map_file(path) : std::make_unique<mmap>(path);
}

std::unique_ptr<mmif>
os_access_generic::map_file(fs::path const& path, size_t size) const {
  return state_ ? state_->map_file(path, size) : std::make_unique<mmap>(path, size);
}

int os_access_generic::access(fs::path const& path, int mode) const {
#ifdef _WIN32
  return ::_waccess(path.wstring().c_str(), mode);
#else
  return ::access(path.string().c_str(), mode);
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

  for (auto cpu : cpus) {
    CPU_SET(cpu, &cpuset);
  }

  if (auto error = pthread_setaffinity_np(std_to_pthread_id(tid),
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
                          std_to_win_thread_id(tid));

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

  auto port = ::pthread_mach_thread_np(std_to_pthread_id(tid));

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

  if (auto err = ::pthread_getcpuclockid(std_to_pthread_id(tid), &cid);
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
  return boost::process::search_path(name.wstring()).wstring();
}

} // namespace dwarfs
