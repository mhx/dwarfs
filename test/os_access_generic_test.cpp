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
#include <filesystem>
#include <latch>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include <folly/portability/PThread.h>
#include <folly/portability/Stdlib.h>

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
#include <dwarfs/detail/scoped_env.h>
#include <dwarfs/file_util.h>
#include <dwarfs/os_access_generic.h>

#include <dwarfs/internal/os_access_generic_data.h>
#include <dwarfs/internal/thread_util.h>

#include "sparse_file_builder.h"
#include "test_helpers.h"

using namespace dwarfs::binary_literals;
using dwarfs::internal::os_access_generic_data;

namespace fs = std::filesystem;

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

TEST(os_access_generic, map_empty_readonly) {
  dwarfs::os_access_generic os;

  auto mapping = os.map_empty_readonly(12345);

  EXPECT_TRUE(mapping.valid());
  EXPECT_EQ(mapping.size(), 12345);

  auto span = mapping.const_span<uint8_t>();
  EXPECT_EQ(span.size(), 12345);

  // mapping should be all zeroes
  EXPECT_THAT(span, testing::Each(0));
}

TEST(os_access_generic, getenv) {
  static constexpr auto test_var{"_DWARFS_OS_ACCESS_TEST_"};
  dwarfs::detail::scoped_env env;

  ASSERT_NO_THROW(env.unset(test_var));

  dwarfs::os_access_generic os;

  {
    auto value = os.getenv(test_var);
    EXPECT_FALSE(value.has_value());
  }

  ASSERT_NO_THROW(env.set(test_var, "some_value"));

  {
    auto value = os.getenv(test_var);
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*value, "some_value");
  }

  ASSERT_NO_THROW(env.set(test_var, ""));

  {
    auto value = os.getenv(test_var);
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*value, "");
  }

  ASSERT_NO_THROW(env.unset(test_var));

  {
    auto value = os.getenv(test_var);
    EXPECT_FALSE(value.has_value());
  }
}

TEST(os_access_generic, symlink_info) {
  using namespace dwarfs;

  temporary_directory td;
  auto const granularity =
      test::sparse_file_builder::hole_granularity(td.path());

  if (!granularity.has_value()) {
    GTEST_SKIP() << "filesystem does not support sparse files";
  }

  std::mt19937_64 rng(42);

  auto const dir = td.path() / "dir";
  auto const file = td.path() / "some_file";
  auto const symlink = td.path() / "some_symlink";
  auto const hardlink = td.path() / "some_hardlink";
  auto const sparse = td.path() / "sparse_file";
  auto const exe_like = td.path() / "some.exe";

  fs::create_directory(dir);
  write_file(file, "hello");
  fs::create_symlink("some_file", symlink);
  fs::create_hard_link(file, hardlink);

  auto sfb = test::sparse_file_builder::create(sparse);
  sfb.truncate(3 * granularity.value());
  sfb.write_data(0, test::create_random_string(granularity.value(), rng));
  sfb.write_data(2 * granularity.value(),
                 test::create_random_string(granularity.value(), rng));
  sfb.punch_hole(granularity.value(), granularity.value());
  sfb.commit();

  write_file(exe_like, "something executable");
  fs::permissions(exe_like,
                  fs::perms::owner_exec | fs::perms::group_exec |
                      fs::perms::others_exec,
                  fs::perm_options::add);

  dwarfs::os_access_generic os;

  auto const st_dir = os.symlink_info(dir);
  auto const st_file = os.symlink_info(file);
  auto const st_symlink = os.symlink_info(symlink);
  auto const st_hardlink = os.symlink_info(hardlink);
  auto const st_sparse = os.symlink_info(sparse);
  auto const st_exe_like = os.symlink_info(exe_like);

  auto is_executable = [](auto const& st) {
    return (fs::perms(st.permissions()) & fs::perms::owner_exec) !=
           fs::perms::none;
  };

  EXPECT_TRUE(st_dir.is_directory());
  EXPECT_TRUE(st_file.is_regular_file());
  EXPECT_TRUE(st_symlink.is_symlink());
  EXPECT_TRUE(st_hardlink.is_regular_file());
  EXPECT_TRUE(st_sparse.is_regular_file());
  EXPECT_TRUE(st_exe_like.is_regular_file());

  EXPECT_GE(st_dir.nlink(), 1);
  EXPECT_EQ(2, st_file.nlink());
  EXPECT_EQ(2, st_hardlink.nlink());
  EXPECT_EQ(1, st_sparse.nlink());
  EXPECT_EQ(1, st_symlink.nlink());
  EXPECT_EQ(1, st_exe_like.nlink());

  EXPECT_TRUE(is_executable(st_dir));
  EXPECT_FALSE(is_executable(st_file));
  EXPECT_TRUE(is_executable(st_symlink));
  EXPECT_FALSE(is_executable(st_hardlink));
  EXPECT_FALSE(is_executable(st_sparse));
  EXPECT_TRUE(is_executable(st_exe_like));

  std::unordered_set<file_stat::dev_type> devs;

  devs.insert(st_dir.dev());
  devs.insert(st_file.dev());
  devs.insert(st_symlink.dev());
  devs.insert(st_hardlink.dev());
  devs.insert(st_sparse.dev());
  devs.insert(st_exe_like.dev());

  EXPECT_EQ(1, devs.size()) << "all files should be on the same device";

  std::unordered_set<file_stat::ino_type> inos;

  inos.insert(st_dir.ino());
  inos.insert(st_file.ino());
  inos.insert(st_symlink.ino());
  inos.insert(st_hardlink.ino());
  inos.insert(st_sparse.ino());
  inos.insert(st_exe_like.ino());

  EXPECT_EQ(st_file.ino(), st_hardlink.ino());
  EXPECT_EQ(5, inos.size()) << "there should be 5 distinct inodes";

  EXPECT_EQ(st_file.size(), 5);
  EXPECT_EQ(st_hardlink.size(), 5);
  EXPECT_EQ(st_sparse.size(), 3 * granularity.value());
  EXPECT_EQ(st_symlink.size(), 9);
  EXPECT_EQ(st_exe_like.size(), 20);

  EXPECT_EQ(st_file.allocated_size(), 5);
  EXPECT_EQ(st_hardlink.allocated_size(), 5);
  EXPECT_EQ(st_sparse.allocated_size(), 2 * granularity.value());
  EXPECT_EQ(st_symlink.allocated_size(), 9);
  EXPECT_EQ(st_exe_like.allocated_size(), 20);

  // directory size is very platform-dependent
  EXPECT_EQ(st_dir.size(), st_dir.allocated_size());
}
