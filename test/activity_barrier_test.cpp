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
#include <cstdint>
#include <future>
#include <optional>
#include <thread>
#include <type_traits>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <dwarfs/internal/activity_barrier.h>

using namespace dwarfs::internal;
using namespace std::chrono_literals;

namespace {

constexpr auto kShortWait = 50ms;
constexpr auto kLongWait = 5s;

template <typename Future>
bool is_ready(Future& future, std::chrono::milliseconds timeout = kShortWait) {
  return future.wait_for(timeout) == std::future_status::ready;
}

} // namespace

TEST(activity_barrier_test, token_is_move_only) {
  static_assert(!std::is_copy_constructible_v<activity_barrier::token>);
  static_assert(!std::is_copy_assignable_v<activity_barrier::token>);
  static_assert(std::is_move_constructible_v<activity_barrier::token>);
  static_assert(std::is_move_assignable_v<activity_barrier::token>);
}

TEST(activity_barrier_test, wait_returns_immediately_when_no_activity_exists) {
  activity_barrier barrier;

  auto const epoch = barrier.begin_new_epoch();

  EXPECT_NO_THROW(barrier.wait_for_older_activity(epoch));
}

TEST(activity_barrier_test, old_activity_blocks_until_token_is_destroyed) {
  activity_barrier barrier;

  std::optional<activity_barrier::token> activity;
  activity.emplace(barrier.enter());

  auto const epoch = barrier.begin_new_epoch();

  auto waiter = std::async(std::launch::async,
                           [&] { barrier.wait_for_older_activity(epoch); });

  EXPECT_FALSE(is_ready(waiter));

  activity.reset();

  EXPECT_TRUE(is_ready(waiter, kLongWait));
}

TEST(activity_barrier_test,
     newer_activity_does_not_block_wait_for_older_epoch) {
  activity_barrier barrier;

  std::optional<activity_barrier::token> old_activity;
  old_activity.emplace(barrier.enter());

  auto const old_epoch = barrier.begin_new_epoch();

  std::optional<activity_barrier::token> new_activity;
  new_activity.emplace(barrier.enter());

  auto waiter = std::async(std::launch::async,
                           [&] { barrier.wait_for_older_activity(old_epoch); });

  EXPECT_FALSE(is_ready(waiter));

  old_activity.reset();

  EXPECT_TRUE(is_ready(waiter, kLongWait));

  // The newer activity was still alive while the waiter completed.
  new_activity.reset();
}

TEST(activity_barrier_test,
     activity_entered_after_wait_starts_does_not_block_waiter) {
  activity_barrier barrier;

  std::optional<activity_barrier::token> old_activity;
  old_activity.emplace(barrier.enter());

  auto const old_epoch = barrier.begin_new_epoch();

  auto waiter = std::async(std::launch::async,
                           [&] { barrier.wait_for_older_activity(old_epoch); });

  EXPECT_FALSE(is_ready(waiter));

  std::optional<activity_barrier::token> new_activity;
  new_activity.emplace(barrier.enter());

  old_activity.reset();

  EXPECT_TRUE(is_ready(waiter, kLongWait));

  new_activity.reset();
}

TEST(activity_barrier_test, moved_token_keeps_activity_alive) {
  activity_barrier barrier;

  std::optional<activity_barrier::token> original;
  original.emplace(barrier.enter());

  auto const epoch = barrier.begin_new_epoch();

  std::optional<activity_barrier::token> moved;
  moved.emplace(std::move(*original));

  original.reset();

  auto waiter = std::async(std::launch::async,
                           [&] { barrier.wait_for_older_activity(epoch); });

  EXPECT_FALSE(is_ready(waiter));

  moved.reset();

  EXPECT_TRUE(is_ready(waiter, kLongWait));
}

TEST(activity_barrier_test, move_assignment_releases_previous_activity) {
  activity_barrier barrier;

  auto older_activity = barrier.enter();

  auto const older_epoch = barrier.begin_new_epoch();

  auto newer_activity = barrier.enter();

  newer_activity = std::move(older_activity);

  auto waiter = std::async(std::launch::async, [&] {
    barrier.wait_for_older_activity(older_epoch);
  });

  EXPECT_FALSE(is_ready(waiter));

  // newer_activity now owns the older activity after move-assignment.
  newer_activity = barrier.enter();

  EXPECT_TRUE(is_ready(waiter, kLongWait));
}

TEST(activity_barrier_test, multiple_concurrent_activities_are_all_waited_for) {
  activity_barrier barrier;

  constexpr std::size_t num_threads = 8;

  std::atomic<std::size_t> entered{0};

  std::promise<void> release_promise;
  auto release_future = release_promise.get_future().share();

  std::vector<std::thread> threads;
  threads.reserve(num_threads);

  for (std::size_t i = 0; i < num_threads; ++i) {
    threads.emplace_back([&] {
      auto activity = barrier.enter();

      entered.fetch_add(1, std::memory_order_release);

      release_future.wait();
    });
  }

  while (entered.load(std::memory_order_acquire) != num_threads) {
    std::this_thread::yield();
  }

  auto const epoch = barrier.begin_new_epoch();

  auto waiter = std::async(std::launch::async,
                           [&] { barrier.wait_for_older_activity(epoch); });

  EXPECT_FALSE(is_ready(waiter));

  release_promise.set_value();

  for (auto& thread : threads) {
    thread.join();
  }

  EXPECT_TRUE(is_ready(waiter, kLongWait));
}

TEST(activity_barrier_test,
     wait_for_older_activity_can_be_called_multiple_times) {
  activity_barrier barrier;

  std::optional<activity_barrier::token> activity;
  activity.emplace(barrier.enter());

  auto const first_epoch = barrier.begin_new_epoch();

  auto waiter = std::async(std::launch::async, [&] {
    barrier.wait_for_older_activity(first_epoch);
  });

  EXPECT_FALSE(is_ready(waiter));

  activity.reset();

  EXPECT_TRUE(is_ready(waiter, kLongWait));

  auto const second_epoch = barrier.begin_new_epoch();

  EXPECT_NO_THROW(barrier.wait_for_older_activity(second_epoch));
}
