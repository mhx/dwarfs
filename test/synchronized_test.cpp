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
#include <future>
#include <latch>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <dwarfs/internal/synchronized.h>

using namespace std::chrono_literals;
using dwarfs::internal::synchronized;

namespace {

struct widget {
  int value = 0;
  void bump() { ++value; }
};

} // namespace

static_assert(!std::is_copy_constructible_v<synchronized<int>>);
static_assert(!std::is_copy_assignable_v<synchronized<int>>);
static_assert(!std::is_move_constructible_v<synchronized<int>>);
static_assert(!std::is_move_assignable_v<synchronized<int>>);

TEST(synchronized_test, lock_grants_mutable_access) {
  synchronized<widget> s(std::in_place);

  {
    auto lk = s.lock();
    lk->value = 41;
    lk->bump();
    EXPECT_EQ(lk->value, 42);

    (*lk).bump();
    EXPECT_EQ((*lk).value, 43);

    EXPECT_EQ(lk.get(), &(*lk));
  }

  auto v = s.with_lock([](widget& w) { return w.value; });
  EXPECT_EQ(v, 43);
}

TEST(synchronized_test, const_lock_grants_const_access) {
  synchronized<std::string> const s(std::in_place, "hello");

  auto lk = s.lock();

  static_assert(std::is_same_v<decltype(*lk), std::string const&>);
  static_assert(std::is_same_v<decltype(lk.get()), std::string const*>);

  EXPECT_EQ(lk->size(), 5u);
  EXPECT_EQ(*lk, "hello");
}

TEST(synchronized_test, with_lock_invokes_callable_and_can_mutate) {
  synchronized<std::vector<int>> s(std::in_place);

  testing::MockFunction<void(std::vector<int>&)> mock;
  EXPECT_CALL(mock, Call(testing::_))
      .Times(1)
      .WillOnce([](std::vector<int>& v) { v.push_back(7); });

  s.with_lock([&](auto& v) { mock.Call(v); });

  auto size = s.with_lock([](auto& v) { return v.size(); });
  EXPECT_EQ(size, 1u);

  EXPECT_EQ(7, s.lock()->front());
}

TEST(synchronized_test,
     with_lock_returns_by_value_even_if_callable_returns_reference) {
  synchronized<int> s(123);

  auto r = s.with_lock([](int& x) -> int& { return x; });
  static_assert(std::is_same_v<decltype(r), int>);
  EXPECT_EQ(r, 123);

  s.with_lock([](int& x) { x = 456; });

  auto r2 = s.with_lock([](int& x) -> int& { return x; });
  EXPECT_EQ(r2, 456);
}

TEST(synchronized_test,
     lock_holds_mutex_for_scope_other_thread_blocks_until_released) {
  synchronized<int> s(0);

  std::latch thread_started(1);
  std::atomic<bool> acquired{false};

  auto lk = std::make_optional(s.lock());

  std::thread t([&] {
    thread_started.count_down();
    s.with_lock([&](int& x) {
      acquired.store(true, std::memory_order_release);
      ++x;
    });
  });

  thread_started.wait();

  // Give the other thread a moment to attempt acquiring the lock.
  std::this_thread::sleep_for(20ms);

  // It should not have acquired yet because we still hold lk.
  EXPECT_FALSE(acquired.load(std::memory_order_acquire));

  // Release lock and let the other thread proceed.
  lk.reset();

  t.join();

  EXPECT_TRUE(acquired.load(std::memory_order_acquire));
  EXPECT_EQ(*s.lock(), 1);
}

TEST(synchronized_test, concurrent_with_lock_increments_are_correct) {
  synchronized<int> s(0);

  constexpr int num_threads = 8;
  constexpr int iters = 50'000;

  std::vector<std::thread> threads;
  threads.reserve(num_threads);

  for (int i = 0; i < num_threads; ++i) {
    if (i % 2 == 0) {
      threads.emplace_back([&] {
        for (int j = 0; j < iters; ++j) {
          ++(*s.lock());
        }
      });
    } else {
      threads.emplace_back([&] {
        for (int j = 0; j < iters; ++j) {
          s.with_lock([](int& x) { ++x; });
        }
      });
    }
  }

  for (auto& th : threads) {
    th.join();
  }

  EXPECT_EQ(num_threads * iters, *s.lock());
}

TEST(synchronized_test, store_and_load) {
  synchronized<int> s(0);

  s.store(42);
  EXPECT_EQ(s.load(), 42);
}

using shared_sync_t = synchronized<int, std::shared_mutex>;

static_assert(shared_sync_t::is_shared);

static_assert(requires(shared_sync_t s) { s.wlock(); });
static_assert(requires(shared_sync_t s) { s.with_wlock([](int&) {}); });

static_assert(requires(shared_sync_t const s) { s.rlock(); });
static_assert(requires(shared_sync_t const s) {
  s.with_rlock([](int const&) {});
});

TEST(synchronized_shared_test,
     rlock_provides_const_access_even_on_non_const_object) {
  synchronized<int, std::shared_mutex> s(123);

  auto lk = s.rlock();
  static_assert(std::is_same_v<decltype(*lk), int const&>);
  static_assert(std::is_same_v<decltype(lk.get()), int const*>);

  EXPECT_EQ(*lk, 123);
}

TEST(synchronized_shared_test,
     with_rlock_returns_by_value_even_if_callable_returns_reference) {
  synchronized<int, std::shared_mutex> s(7);

  auto v = s.with_rlock([](int const& x) -> int const& { return x; });
  static_assert(std::is_same_v<decltype(v), int>);
  EXPECT_EQ(v, 7);
}

TEST(synchronized_shared_test, with_wlock_can_mutate_and_returns_by_value) {
  synchronized<int, std::shared_mutex> s(1);

  s.with_wlock([](int& x) { x = 42; });

  auto v = s.with_rlock([](int const& x) { return x; });
  EXPECT_EQ(v, 42);

  auto by_value = s.with_wlock([](int& x) -> int& { return x; });
  static_assert(std::is_same_v<decltype(by_value), int>);
  EXPECT_EQ(by_value, 42);
}

TEST(synchronized_shared_test, two_readers_can_acquire_concurrently) {
  synchronized<int, std::shared_mutex> s(0);

  std::promise<void> r1_acquired_p;
  std::promise<void> r2_acquired_p;
  std::promise<void> release_p;

  auto r1_acquired = r1_acquired_p.get_future();
  auto r2_acquired = r2_acquired_p.get_future();
  auto release_fut = release_p.get_future().share();

  std::thread t1([&] {
    auto lk = s.rlock();
    r1_acquired_p.set_value();
    release_fut.wait(); // keep rlock held
  });

  ASSERT_EQ(r1_acquired.wait_for(200ms), std::future_status::ready);

  std::thread t2([&] {
    auto lk = s.rlock(); // should be able to acquire while t1 holds rlock
    r2_acquired_p.set_value();
  });

  EXPECT_EQ(r2_acquired.wait_for(200ms), std::future_status::ready);

  release_p.set_value();
  t2.join();
  t1.join();
}

TEST(synchronized_shared_test, writer_blocks_reader_until_released) {
  synchronized<int, std::shared_mutex> s(0);

  // hold writer lock in main thread
  auto wlk = std::make_optional(s.wlock());

  std::promise<void> reader_acquired_p;
  auto reader_acquired = reader_acquired_p.get_future();

  std::thread reader([&] {
    auto rlk = s.rlock(); // should block until wlk is released
    reader_acquired_p.set_value();
  });

  // reader should not acquire while writer is held
  EXPECT_EQ(reader_acquired.wait_for(100ms), std::future_status::timeout);

  // release writer lock; reader should acquire soon after
  wlk.reset();
  ASSERT_EQ(reader_acquired.wait_for(2s), std::future_status::ready);

  reader.join();
}

TEST(synchronized_shared_test, writer_blocks_writer_until_released) {
  synchronized<int, std::shared_mutex> s(0);

  auto wlk1 = std::make_optional(s.wlock());

  std::promise<void> writer2_acquired_p;
  auto writer2_acquired = writer2_acquired_p.get_future();

  std::thread writer2([&] {
    auto wlk2 = s.wlock(); // should block until wlk1 released
    writer2_acquired_p.set_value();
  });

  EXPECT_EQ(writer2_acquired.wait_for(100ms), std::future_status::timeout);

  wlk1.reset();
  ASSERT_EQ(writer2_acquired.wait_for(2s), std::future_status::ready);

  writer2.join();
}

TEST(synchronized_shared_test, store_and_load) {
  synchronized<int, std::shared_mutex> s(0);

  s.store(42);
  EXPECT_EQ(s.load(), 42);
}
