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

#include <atomic>
#include <chrono>
#include <unordered_set>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <dwarfs/thread_pool.h>

#include "test_helpers.h"
#include "test_logger.h"

using namespace dwarfs;
using namespace std::chrono_literals;

TEST(thread_pool, basic) {
  test::os_access_mock os;
  test::test_logger lgr;
  thread_pool pool;
  std::unordered_set<std::thread::id> seen_thread_ids;
  int get_cpu_time_calls{0};

  os.set_thread_get_cpu_time_impl(
      [&](std::thread::id tid,
          std::error_code& ec) -> std::chrono::nanoseconds {
        ec.clear();
        ++get_cpu_time_calls;
        seen_thread_ids.emplace(tid);
        return 25ms;
      });

  EXPECT_FALSE(pool);

  pool = thread_pool(lgr, os, "testpool", 4);

  ASSERT_TRUE(pool);
  EXPECT_TRUE(pool.running());

  EXPECT_EQ(4, pool.num_workers());

  EXPECT_EQ(0, get_cpu_time_calls);

  auto cpu_time = pool.try_get_cpu_time();

  EXPECT_EQ(4, get_cpu_time_calls);
  EXPECT_EQ(4, seen_thread_ids.size());

  ASSERT_TRUE(cpu_time.has_value());
  EXPECT_EQ(100ms, cpu_time.value());

  std::atomic<int> counter{0};

  for (int i = 0; i < 16; ++i) {
    pool.add_job([&counter] {
      ++counter;
      std::this_thread::sleep_for(10ms);
    });
  }

  pool.wait();

  EXPECT_TRUE(pool.running());
  EXPECT_EQ(16, counter);

  std::error_code ec;
  EXPECT_EQ(100ms, pool.get_cpu_time(ec));
  EXPECT_FALSE(ec);
  EXPECT_EQ(8, get_cpu_time_calls);
  EXPECT_EQ(4, seen_thread_ids.size());

  pool.stop();

  EXPECT_FALSE(pool.running());
}

TEST(thread_pool, get_cpu_time_error) {
  test::os_access_mock os;
  test::test_logger lgr;

  os.set_thread_get_cpu_time_impl(
      [&](std::thread::id, std::error_code& ec) -> std::chrono::nanoseconds {
        ec = std::make_error_code(std::errc::not_supported);
        return 0ms;
      });

  thread_pool source_pool(lgr, os, "testpool", 4);
  thread_pool pool(std::move(source_pool));

  ASSERT_TRUE(pool);
  EXPECT_TRUE(pool.running());
  EXPECT_FALSE(source_pool);

  std::error_code ec;
  EXPECT_EQ(0ms, pool.get_cpu_time(ec));
  EXPECT_EQ(std::errc::not_supported, ec);

  EXPECT_FALSE(pool.try_get_cpu_time().has_value());
}

TEST(thread_pool, job_throws_exception) {
  test::os_access_mock os;
  test::test_logger lgr;
  thread_pool pool(lgr, os, "testpool", 4);

  pool.add_job([] { throw std::runtime_error("job failed"); });
  pool.wait();

  auto const& log = lgr.get_log();
  ASSERT_EQ(1, log.size());
  EXPECT_EQ(logger::FATAL, log[0].level);
  EXPECT_THAT(log[0].output,
              testing::HasSubstr("exception thrown in worker thread:"));
  EXPECT_THAT(log[0].output, testing::HasSubstr("job failed"));
}
