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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <atomic>
#include <future>
#include <memory>

#include <dwarfs/internal/worker_group.h>

#include "test_helpers.h"
#include "test_logger.h"

using namespace dwarfs;

TEST(worker_group_test, set_thread_affinity_env) {
  test::test_logger lgr;
  test::os_access_mock os;

  os.setenv("DWARFS_WORKER_GROUP_AFFINITY", "lemon=0,1:lime=2,3");

  os.set_affinity_calls.clear();
  internal::worker_group wg_lemon(lgr, os, "lemon", {.num_workers = 2});
  ASSERT_EQ(2, os.set_affinity_calls.size());
  EXPECT_EQ(std::vector<int>({0, 1}), std::get<1>(os.set_affinity_calls[0]));

  os.set_affinity_calls.clear();
  internal::worker_group wg_lime(lgr, os, "lime", {.num_workers = 3});
  ASSERT_EQ(3, os.set_affinity_calls.size());
  EXPECT_EQ(std::vector<int>({2, 3}), std::get<1>(os.set_affinity_calls[0]));

  os.set_affinity_calls.clear();
  internal::worker_group wg_apple(lgr, os, "apple", {.num_workers = 1});
  EXPECT_EQ(0, os.set_affinity_calls.size());
}

TEST(worker_group_test, direct_callable_and_move_only_job) {
  test::test_logger lgr;
  test::os_access_mock os;
  internal::worker_group wg(lgr, os, "jobs", {.num_workers = 3});

  std::atomic<int> sum{0};
  for (int i = 0; i < 100; ++i) {
    ASSERT_TRUE(wg.add_job([&sum] { sum.fetch_add(1); }));
  }

  auto owned = std::make_unique<int>(23);
  ASSERT_TRUE(
      wg.add_job([&sum, value = std::move(owned)] { sum.fetch_add(*value); }));
  EXPECT_FALSE(owned);

  std::packaged_task<int()> task([] { return 42; });
  auto future = task.get_future();
  ASSERT_TRUE(wg.add_job(std::move(task)));

  wg.wait();
  EXPECT_EQ(123, sum.load());
  EXPECT_EQ(42, future.get());
}

TEST(worker_group_test, typed_job_arguments) {
  test::test_logger lgr;
  test::os_access_mock os;

  internal::basic_worker_group<int> by_value(lgr, os, "value",
                                             {.num_workers = 2}, 7);

  for (int i = 0; i < 10; ++i) {
    ASSERT_TRUE(by_value.add_job([](int&& value) { value = 99; }));
  }

  std::atomic<int> sum{0};

  for (int i = 0; i < 100; ++i) {
    ASSERT_TRUE(by_value.add_job([&sum](int value) { sum.fetch_add(value); }));
  }

  by_value.wait();

  EXPECT_EQ(700, sum.load());

  int shared = 0;
  internal::basic_worker_group<int&> by_reference(lgr, os, "reference",
                                                  {.num_workers = 1}, shared);

  for (int i = 0; i < 100; ++i) {
    ASSERT_TRUE(by_reference.add_job([](int& value) { ++value; }));
  }

  by_reference.wait();

  EXPECT_EQ(100, shared);
}

TEST(worker_group_test, rejected_job_releases_capture) {
  test::test_logger lgr;
  test::os_access_mock os;
  internal::worker_group wg(lgr, os, "stopped", {.num_workers = 1});
  wg.stop();

  auto owner = std::make_shared<int>(13);
  auto const weak = std::weak_ptr<int>(owner);

  EXPECT_FALSE(wg.add_job([captured = std::move(owner)] {}));
  EXPECT_TRUE(weak.expired());
}
