/* vim:set ts=2 sw=2 sts=2 et: */
/**
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
#include <future>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <dwarfs/internal/ordered_preparation_queue.h>
#include <dwarfs/internal/worker_group.h>

#include "test_helpers.h"
#include "test_logger.h"

using namespace dwarfs;
using namespace std::chrono_literals;

TEST(ordered_preparation_queue_test, rejects_zero_capacity_and_empty_pop) {
  test::test_logger lgr;
  test::os_access_mock os;
  internal::worker_group workers(lgr, os, "prep", {.num_workers = 2});

  EXPECT_THROW((internal::ordered_preparation_queue<int>(workers, 0)),
               std::invalid_argument);

  internal::ordered_preparation_queue<int> queue(workers, 2);

  EXPECT_EQ(2, queue.capacity());
  EXPECT_TRUE(queue.empty());
  EXPECT_THROW(queue.pop(), std::logic_error);
}

TEST(ordered_preparation_queue_test,
     bounded_and_fifo_even_if_jobs_finish_out_of_order) {
  test::test_logger lgr;
  test::os_access_mock os;
  internal::worker_group workers(lgr, os, "prep", {.num_workers = 2});
  internal::ordered_preparation_queue<int> queue(workers, 2);

  std::promise<void> release_first;
  auto gate = release_first.get_future().share();

  std::promise<void> second_done;
  auto second_ready = second_done.get_future();

  queue.submit([gate] {
    gate.wait();
    return 1;
  });

  queue.submit([&second_done] {
    second_done.set_value();
    return 2;
  });

  ASSERT_EQ(second_ready.wait_for(5s), std::future_status::ready);
  EXPECT_TRUE(queue.full());
  EXPECT_EQ(2, queue.size());
  EXPECT_THROW(queue.submit([] { return 3; }), std::logic_error);

  release_first.set_value();
  EXPECT_EQ(queue.pop(), 1);
  EXPECT_EQ(queue.size(), 1);

  queue.submit([] { return 3; });
  EXPECT_EQ(queue.pop(), 2);
  EXPECT_EQ(queue.pop(), 3);
  EXPECT_TRUE(queue.empty());
}

TEST(ordered_preparation_queue_test,
     exceptions_do_not_poison_following_results) {
  test::test_logger lgr;
  test::os_access_mock os;
  internal::worker_group workers(lgr, os, "prep", {.num_workers = 1});
  internal::ordered_preparation_queue<int> queue(workers, 2);

  queue.submit([]() -> int { throw std::runtime_error("open failed"); });
  queue.submit([] { return 42; });

  EXPECT_THROW(queue.pop(), std::runtime_error);
  EXPECT_EQ(queue.pop(), 42);
  EXPECT_TRUE(queue.empty());
}

TEST(ordered_preparation_queue_test,
     multiple_queues_share_workers_and_are_independent) {
  test::test_logger lgr;
  test::os_access_mock os;
  internal::worker_group workers(lgr, os, "prep", {.num_workers = 2});
  internal::ordered_preparation_queue<int> cat_a(workers, 1);
  internal::ordered_preparation_queue<int> cat_b(workers, 2);

  std::promise<void> release_a;
  auto gate = release_a.get_future().share();

  cat_a.submit([gate] {
    gate.wait();
    return 10;
  });
  cat_b.submit([] { return 20; });

  EXPECT_EQ(cat_b.pop(), 20);
  EXPECT_TRUE(cat_a.full());

  release_a.set_value();
  EXPECT_EQ(cat_a.pop(), 10);
}

TEST(ordered_preparation_queue_test, rejected_submission_does_not_occupy_slot) {
  test::test_logger lgr;
  test::os_access_mock os;
  internal::worker_group workers(lgr, os, "prep", {.num_workers = 1});
  workers.stop();
  internal::ordered_preparation_queue<int> queue(workers, 1);
  EXPECT_THROW(queue.submit([] { return 42; }), std::runtime_error);
  EXPECT_TRUE(queue.empty());
}

TEST(ordered_preparation_queue_test, move_only_result_and_fragment_context) {
  test::test_logger lgr;
  test::os_access_mock os;
  internal::worker_group workers(lgr, os, "prep", {.num_workers = 1});
  internal::ordered_preparation_queue<std::unique_ptr<int>> queue(workers, 1);

  auto fragment_sizes = std::vector<int>{5, 7, 11};
  queue.submit([ranges = std::move(fragment_sizes)] {
    return std::make_unique<int>(ranges[0] + ranges[2]);
  });
  auto result = queue.pop();
  ASSERT_NE(result, nullptr);
  EXPECT_EQ(*result, 16);
}

TEST(ordered_preparation_queue_test, destruction_waits_for_outstanding_jobs) {
  test::test_logger lgr;
  test::os_access_mock os;
  internal::worker_group workers(lgr, os, "prep", {.num_workers = 1});
  std::promise<void> release;
  auto gate = release.get_future().share();
  std::atomic<bool> completed{false};
  std::promise<void> submitted;
  auto submitted_future = submitted.get_future();

  auto owner = std::async(std::launch::async, [&] {
    internal::ordered_preparation_queue<int> queue(workers, 1);
    queue.submit([&completed, gate] {
      gate.wait();
      completed.store(true);
      return 1;
    });
    submitted.set_value();
  });

  ASSERT_EQ(submitted_future.wait_for(5s), std::future_status::ready);
  EXPECT_EQ(owner.wait_for(50ms), std::future_status::timeout);
  release.set_value();
  EXPECT_EQ(owner.wait_for(5s), std::future_status::ready);
  owner.get();
  EXPECT_TRUE(completed.load());
}

TEST(prepared_range_test, synchronous_preparation_is_lazy_and_paired) {
  auto inputs = std::vector<int>{3, 1, 4, 1, 5};
  std::vector<int> prepared_inputs;
  auto range = internal::prepare_ordered(inputs, nullptr, 0, [&](int input) {
    prepared_inputs.push_back(input);
    return std::to_string(input * 2);
  });

  EXPECT_TRUE(prepared_inputs.empty());
  std::vector<std::pair<int, std::string>> results;

  for (auto&& [input, result] : range) {
    EXPECT_EQ(prepared_inputs.size(), results.size() + 1);
    results.emplace_back(input, result);
  }

  EXPECT_EQ((std::vector<int>{3, 1, 4, 1, 5}), prepared_inputs);
  EXPECT_EQ((std::vector<std::pair<int, std::string>>{
                {3, "6"}, {1, "2"}, {4, "8"}, {1, "2"}, {5, "10"}}),
            results);
}

TEST(prepared_range_test, synchronous_preparation_is_not_eager_on_early_exit) {
  std::vector<int> inputs(100);
  std::iota(inputs.begin(), inputs.end(), 0);
  int count{0};
  {
    auto range = internal::prepare_ordered(inputs, nullptr, 64, [&](int x) {
      ++count;
      return x + 1;
    });

    for (auto&& [input, output] : range) {
      EXPECT_EQ(input + 1, output);
      break;
    }
  }
  EXPECT_EQ(count, 1);
}

TEST(prepared_range_test, asynchronous_results_stay_paired_and_ordered) {
  test::test_logger lgr;
  test::os_access_mock os;
  internal::worker_group workers(lgr, os, "prep", {.num_workers = 2});
  std::vector<int> inputs{4, 3, 2, 1, 0};

  auto range = internal::prepare_ordered(inputs, &workers, 2, [](int x) {
    if (x == 4) {
      std::this_thread::sleep_for(20ms);
    }
    return std::make_unique<int>(100 + x);
  });

  int expected{4};
  for (auto&& [input, result] : range) {
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(input, expected);
    EXPECT_EQ(*result, 100 + input);
    --expected;
  }

  EXPECT_EQ(expected, -1);
}

TEST(prepared_range_test, asynchronous_failure_propagates) {
  test::test_logger lgr;
  test::os_access_mock os;
  internal::worker_group workers(lgr, os, "prep", {.num_workers = 2});
  std::vector<int> inputs{1, 2, 3};

  auto range = internal::prepare_ordered(inputs, &workers, 2, [](int x) {
    if (x == 2) {
      throw std::runtime_error("preparation failed");
    }
    return x * 2;
  });

  auto it = range.begin();

  ASSERT_NE(it, range.end());
  EXPECT_EQ((*it).first, 1);
  EXPECT_EQ((*it).second, 2);
  EXPECT_THROW(++it, std::runtime_error);
}

TEST(prepared_range_test, empty_range_with_and_without_workers) {
  test::test_logger lgr;
  test::os_access_mock os;
  internal::worker_group workers(lgr, os, "prep", {.num_workers = 2});
  std::vector<int> inputs;
  auto sync =
      internal::prepare_ordered(inputs, nullptr, 0, [](int x) { return x; });
  auto async =
      internal::prepare_ordered(inputs, &workers, 3, [](int x) { return x; });
  EXPECT_EQ(sync.begin(), sync.end());
  EXPECT_EQ(async.begin(), async.end());
  EXPECT_EQ(decltype(sync.begin()){}, sync.end());
}
