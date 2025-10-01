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
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <chrono>
#include <latch>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>

#include <folly/portability/PThread.h>

#ifdef _WIN32
#include <folly/portability/Windows.h>
#else
#include <sys/resource.h>
#endif

#ifdef __FreeBSD__
#include <pthread_np.h>
#endif

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <dwarfs/binary_literals.h>
#include <dwarfs/os_access_generic.h>

#include <dwarfs/internal/os_access_generic_data.h>
#include <dwarfs/internal/thread_util.h>

using namespace dwarfs::binary_literals;
using dwarfs::internal::os_access_generic_data;

namespace {

class test_env {
 public:
  void set(std::string const& var, std::string const& value) {
    vars_[var] = value;
  }

  char const* operator()(char const* var) const {
    auto const it = vars_.find(var);
    return it == vars_.end() ? nullptr : it->second.c_str();
  }

 private:
  std::unordered_map<std::string, std::string> vars_;
};

#if !(defined(_WIN32) || defined(__APPLE__))
std::vector<int> get_affinity(std::thread::id tid) {
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);

  auto handle = dwarfs::internal::std_to_pthread_id(tid);

  if (pthread_getaffinity_np(handle, sizeof(cpu_set_t), &cpuset) != 0) {
    throw std::runtime_error("pthread_getaffinity_np failed");
  }

  std::vector<int> result;

  for (int i = 0; i < CPU_SETSIZE; ++i) {
    if (CPU_ISSET(i, &cpuset)) {
      result.push_back(i);
    }
  }

  return result;
}
#endif

std::chrono::nanoseconds process_cpu_time() {
#ifdef _WIN32
  FILETIME create{}, exit{}, kernel{}, user{};
  if (!::GetProcessTimes(::GetCurrentProcess(), &create, &exit, &kernel,
                         &user)) {
    throw std::system_error(::GetLastError(), std::system_category(),
                            "GetProcessTimes failed");
  }

  ULARGE_INTEGER k{}, u{};
  k.LowPart = kernel.dwLowDateTime;
  k.HighPart = kernel.dwHighDateTime;
  u.LowPart = user.dwLowDateTime;
  u.HighPart = user.dwHighDateTime;

  // FILETIME durations are in 100-ns units
  unsigned long long total_100ns = k.QuadPart + u.QuadPart;
  return std::chrono::nanoseconds(total_100ns * 100ull);
#else
  struct rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) != 0) {
    throw std::system_error(errno, std::system_category(), "getrusage failed");
  }

  auto const sec = static_cast<unsigned long long>(usage.ru_utime.tv_sec) +
                   static_cast<unsigned long long>(usage.ru_stime.tv_sec);
  auto const usec = static_cast<unsigned long long>(usage.ru_utime.tv_usec) +
                    static_cast<unsigned long long>(usage.ru_stime.tv_usec);

  return std::chrono::seconds(sec) + std::chrono::microseconds(usec);
#endif
}

} // namespace

TEST(os_access_generic_data, empty_environment) {
  test_env env;
  std::ostringstream err;

  os_access_generic_data data{err, env};

  EXPECT_TRUE(err.str().empty());

  if (sizeof(void*) == 4) {
    EXPECT_EQ(data.fv_opts().max_eager_map_size, 32_MiB);
  } else {
    EXPECT_FALSE(data.fv_opts().max_eager_map_size.has_value());
  }
}

TEST(os_access_generic_data, valid_max_eager_map_size) {
  test_env env;
  env.set("DWARFS_IOLAYER_OPTS", "max_eager_map_size=64M");
  std::ostringstream err;

  os_access_generic_data data{err, env};

  EXPECT_TRUE(err.str().empty());
  EXPECT_EQ(data.fv_opts().max_eager_map_size, 64_MiB);
}

TEST(os_access_generic_data, valid_max_eager_map_size_unlimited) {
  test_env env;
  env.set("DWARFS_IOLAYER_OPTS", "max_eager_map_size=unlimited");
  std::ostringstream err;

  os_access_generic_data data{err, env};

  EXPECT_TRUE(err.str().empty());
  // regardless of architecture
  EXPECT_FALSE(data.fv_opts().max_eager_map_size.has_value());
}

TEST(os_access_generic_data, invalid_max_eager_map_size) {
  test_env env;
  env.set("DWARFS_IOLAYER_OPTS", "max_eager_map_size=123foo");
  std::ostringstream err;

  os_access_generic_data data{err, env};

  if (sizeof(void*) == 4) {
    EXPECT_EQ(data.fv_opts().max_eager_map_size, 32_MiB);
  } else {
    EXPECT_FALSE(data.fv_opts().max_eager_map_size.has_value());
  }

  EXPECT_THAT(err.str(), testing::HasSubstr(
                             "warning: ignoring invalid DWARFS_IOLAYER_OPTS "
                             "option 'max_eager_map_size'"));
}

TEST(os_access_generic_data, unknown_option) {
  test_env env;
  env.set("DWARFS_IOLAYER_OPTS", "foo=bar");
  std::ostringstream err;

  os_access_generic_data data{err, env};

  if (sizeof(void*) == 4) {
    EXPECT_EQ(data.fv_opts().max_eager_map_size, 32_MiB);
  } else {
    EXPECT_FALSE(data.fv_opts().max_eager_map_size.has_value());
  }

  EXPECT_THAT(
      err.str(),
      testing::HasSubstr(
          "warning: ignoring unknown DWARFS_IOLAYER_OPTS option 'foo'"));
}

TEST(os_access_generic_data, extra_options) {
  test_env env;
  env.set("DWARFS_IOLAYER_OPTS", "foo=bar,max_eager_map_size=64K,someflag");
  std::ostringstream err;

  os_access_generic_data data{err, env};

  EXPECT_EQ(data.fv_opts().max_eager_map_size, 64_KiB);

  EXPECT_THAT(
      err.str(),
      testing::HasSubstr(
          "warning: ignoring unknown DWARFS_IOLAYER_OPTS option 'foo'"));

  EXPECT_THAT(
      err.str(),
      testing::HasSubstr(
          "warning: ignoring unknown DWARFS_IOLAYER_OPTS option 'someflag'"));
}

TEST(os_access_generic, set_thread_affinity) {
#if defined(_WIN32) || defined(__APPLE__)
  GTEST_SKIP() << "thread_set_affinity not supported on this platform";
#else
  auto const num_cpus = std::thread::hardware_concurrency();

  if (num_cpus < 2) {
    GTEST_SKIP() << "This test requires at least two CPUs";
  }

  auto const tid = std::this_thread::get_id();
  auto const original_cpus = get_affinity(tid);

  EXPECT_GT(original_cpus.size(), 0);
  EXPECT_LE(original_cpus.size(), num_cpus);

  dwarfs::os_access_generic os;

  std::vector<int> set_cpus;
  for (size_t i = 1; i < num_cpus; i += 2) {
    set_cpus.push_back(static_cast<int>(i));
  }

  std::error_code ec;
  os.thread_set_affinity(tid, set_cpus, ec);

  EXPECT_FALSE(ec) << ec.message();

  auto const new_cpus = get_affinity(tid);
  EXPECT_THAT(new_cpus, testing::ElementsAreArray(set_cpus));

  // restore original affinity
  os.thread_set_affinity(tid, original_cpus, ec);
  EXPECT_FALSE(ec) << ec.message();

  auto const restored_cpus = get_affinity(tid);
  EXPECT_THAT(restored_cpus, testing::ElementsAreArray(original_cpus));
#endif
}

TEST(os_access_generic, get_thread_cpu_time) {
  using namespace std::chrono_literals;
  std::latch loop_done(1);
  std::latch exit_thread(1);

  std::thread burn_cpu([&] {
    auto const end = process_cpu_time() + 60ms;
    while (process_cpu_time() < end) {
      // burn CPU
    }
    loop_done.count_down();
    exit_thread.wait();
  });

  loop_done.wait();

  dwarfs::os_access_generic os;
  std::error_code ec;

  auto const cpu_time = os.thread_get_cpu_time(burn_cpu.get_id(), ec);

  EXPECT_FALSE(ec) << ec.message();

  exit_thread.count_down();
  burn_cpu.join();

  EXPECT_GE(cpu_time, 40ms);

#ifdef _WIN32
  EXPECT_LE(cpu_time, 120ms);
#else
  EXPECT_LE(cpu_time, 80ms);
#endif
}
