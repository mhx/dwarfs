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

#include <array>
#include <atomic>
#include <cstddef>
#include <memory>
#include <thread>

#include <gtest/gtest.h>

#include <dwarfs/internal/completion_gate.h>

using namespace dwarfs::internal;

TEST(completion_gate_test, only_last_arrival_runs_its_callback) {
  completion_gate<2> gate;
  int result = 0;

  gate.arrive([&] { result = 1; });
  EXPECT_EQ(result, 0);

  gate.arrive([&] { result = 2; });
  EXPECT_EQ(result, 2);
}

TEST(completion_gate_test, either_participant_can_be_last) {
  completion_gate<2> gate;
  int result = 0;

  gate.arrive([&] { result = 2; });
  gate.arrive([&] { result = 1; });

  EXPECT_EQ(result, 1);
}

TEST(completion_gate_test, supports_more_than_two_arrivals) {
  completion_gate<3> gate;
  int calls = 0;

  gate.arrive([&] { ++calls; });
  gate.arrive([&] { ++calls; });
  EXPECT_EQ(calls, 0);

  gate.arrive([&] { ++calls; });
  EXPECT_EQ(calls, 1);
}

TEST(completion_gate_test, accepts_move_only_callbacks) {
  completion_gate<1> gate;
  int result = 0;

  gate.arrive(
      [value = std::make_unique<int>(42), &result] { result = *value; });

  EXPECT_EQ(result, 42);
}

TEST(completion_gate_test, concurrent_arrivals_publish_prior_writes) {
  constexpr std::size_t num_threads = 8;
  completion_gate<num_threads> gate;
  std::array<int, num_threads> values{};
  std::atomic<int> callback_count{0};
  std::atomic<bool> saw_all_writes{false};
  std::array<std::thread, num_threads> threads;

  for (std::size_t i = 0; i < num_threads; ++i) {
    threads[i] = std::thread([&, i] {
      values[i] = static_cast<int>(i + 1);
      gate.arrive([&] {
        bool all_written = true;
        for (std::size_t j = 0; j < num_threads; ++j) {
          all_written &= values[j] == static_cast<int>(j + 1);
        }
        saw_all_writes.store(all_written, std::memory_order_relaxed);
        callback_count.fetch_add(1, std::memory_order_relaxed);
      });
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }

  EXPECT_EQ(callback_count.load(), 1);
  EXPECT_TRUE(saw_all_writes.load());
}
