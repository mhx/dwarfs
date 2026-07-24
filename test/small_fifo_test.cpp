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

#include <future>
#include <memory>
#include <numeric>
#include <utility>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <dwarfs/container/small_fifo.h>

using dwarfs::container::small_fifo;
using testing::ElementsAre;
using testing::ElementsAreArray;
using testing::IsEmpty;

namespace {

template <typename Fifo>
auto drain(Fifo& f) {
  std::vector<std::decay_t<decltype(f.front())>> out;
  while (!f.empty()) {
    out.push_back(std::move(f.front()));
    f.pop_front();
  }
  return out;
}

struct counters {
  int live{0};
  int constructed{0};
  int destroyed{0};

  void reset() { *this = counters{}; }
};

counters& stats() {
  static counters c;
  return c;
}

class tracked {
 public:
  static constexpr int kMovedFrom = -1;

  explicit tracked(int v) noexcept
      : value_{v} {
    ++stats().live;
    ++stats().constructed;
  }

  tracked(tracked&& other) noexcept
      : value_{std::exchange(other.value_, kMovedFrom)} {
    ++stats().live;
    ++stats().constructed;
  }

  tracked& operator=(tracked&& other) noexcept {
    value_ = std::exchange(other.value_, kMovedFrom);
    return *this;
  }

  tracked(tracked const&) = delete;
  tracked& operator=(tracked const&) = delete;

  ~tracked() {
    --stats().live;
    ++stats().destroyed;
  }

  int value() const noexcept { return value_; }

 private:
  int value_;
};

class small_fifo_tracked_test : public ::testing::Test {
 protected:
  void SetUp() override { stats().reset(); }

  void TearDown() override {
    // Every fifo under test is destroyed by now.
    EXPECT_EQ(0, stats().live) << "objects leaked";
    EXPECT_EQ(stats().constructed, stats().destroyed);
  }
};

} // namespace

// --- basic behaviour, across a range of inline capacities -------------------

template <typename N>
class small_fifo_test : public ::testing::Test {};

using inline_sizes = ::testing::Types<
    std::integral_constant<size_t, 1>, std::integral_constant<size_t, 2>,
    std::integral_constant<size_t, 4>, std::integral_constant<size_t, 16>>;

TYPED_TEST_SUITE(small_fifo_test, inline_sizes);

TYPED_TEST(small_fifo_test, empty_on_construction) {
  small_fifo<int, TypeParam::value> f;
  EXPECT_TRUE(f.empty());
  EXPECT_EQ(0u, f.size());
}

TYPED_TEST(small_fifo_test, preserves_fifo_order) {
  small_fifo<int, TypeParam::value> f;

  for (int i = 0; i < 10; ++i) {
    f.push_back(i);
  }

  EXPECT_EQ(10u, f.size());
  EXPECT_THAT(drain(f), ElementsAre(0, 1, 2, 3, 4, 5, 6, 7, 8, 9));
  EXPECT_TRUE(f.empty());
}

TYPED_TEST(small_fifo_test, size_tracks_pops) {
  small_fifo<int, TypeParam::value> f;

  for (int i = 0; i < 5; ++i) {
    f.push_back(i);
  }

  for (size_t expected = 5; expected > 0; --expected) {
    EXPECT_EQ(expected, f.size());
    EXPECT_FALSE(f.empty());
    f.pop_front();
  }

  EXPECT_TRUE(f.empty());
  EXPECT_EQ(0u, f.size());
}

TYPED_TEST(small_fifo_test, front_is_mutable) {
  small_fifo<int, TypeParam::value> f;
  f.push_back(1);
  f.push_back(2);

  f.front() = 42;

  EXPECT_THAT(drain(f), ElementsAre(42, 2));
}

TYPED_TEST(small_fifo_test, clear_empties) {
  small_fifo<int, TypeParam::value> f;

  for (int i = 0; i < 7; ++i) {
    f.push_back(i);
  }

  f.pop_front();
  f.clear();

  EXPECT_TRUE(f.empty());
  EXPECT_EQ(0u, f.size());

  f.push_back(99);
  EXPECT_THAT(drain(f), ElementsAre(99));
}

TYPED_TEST(small_fifo_test, emplace_back_forwards) {
  small_fifo<std::pair<int, int>, TypeParam::value> f;

  f.emplace_back(1, 2);
  f.emplace_back(3, 4);

  EXPECT_EQ(2u, f.size());
  EXPECT_EQ(std::make_pair(1, 2), f.front());
  f.pop_front();
  EXPECT_EQ(std::make_pair(3, 4), f.front());
}

// --- the drain / refill cycle the file_reader actually exercises ------------

TYPED_TEST(small_fifo_test, drain_and_refill_repeatedly) {
  small_fifo<int, TypeParam::value> f;

  for (int round = 0; round < 100; ++round) {
    for (int i = 0; i < 5; ++i) {
      f.push_back(round * 10 + i);
    }

    EXPECT_THAT(drain(f),
                ElementsAre(round * 10 + 0, round * 10 + 1, round * 10 + 2,
                            round * 10 + 3, round * 10 + 4));
    EXPECT_TRUE(f.empty());
  }
}

// Produce a couple, consume one -- the queue never fully drains, so this is
// the path where dead slots would accumulate without compaction.
TYPED_TEST(small_fifo_test, interleaved_push_pop_long_run) {
  small_fifo<int, TypeParam::value> f;
  int next_in{0};
  int next_out{0};

  for (int i = 0; i < 10'000; ++i) {
    f.push_back(next_in++);
    f.push_back(next_in++);

    ASSERT_FALSE(f.empty());
    ASSERT_EQ(next_out++, f.front());
    f.pop_front();
  }

  // Everything still in order after all that compaction.
  ASSERT_EQ(static_cast<size_t>(next_in - next_out), f.size());

  while (!f.empty()) {
    ASSERT_EQ(next_out++, f.front());
    f.pop_front();
  }

  EXPECT_EQ(next_in, next_out);
}

// --- append ----------------------------------------------------------------

TEST(small_fifo_append_test, append_to_empty) {
  small_fifo<int, 2> f;
  std::vector<int> in{1, 2, 3};

  f.append(std::move(in));

  EXPECT_THAT(drain(f), ElementsAre(1, 2, 3));
}

TEST(small_fifo_append_test, append_to_partially_drained) {
  small_fifo<int, 2> f;

  for (int i = 0; i < 3; ++i) {
    f.push_back(i);
  }
  f.pop_front();

  std::vector<int> in{10, 11};
  f.append(std::move(in));

  EXPECT_THAT(drain(f), ElementsAre(1, 2, 10, 11));
}

TEST(small_fifo_append_test, append_after_full_drain_reuses_storage) {
  small_fifo<int, 2> f;

  for (int i = 0; i < 5; ++i) {
    f.push_back(i);
  }
  drain(f);
  ASSERT_TRUE(f.empty());

  std::vector<int> in{7, 8};
  f.append(std::move(in));

  EXPECT_THAT(drain(f), ElementsAre(7, 8));
}

TEST(small_fifo_append_test, append_empty_range_is_noop) {
  small_fifo<int, 2> f;
  f.push_back(1);

  std::vector<int> in;
  f.append(std::move(in));

  EXPECT_EQ(1u, f.size());
  EXPECT_THAT(drain(f), ElementsAre(1));
}

// --- move-only element types -----------------------------------------------

TEST(small_fifo_move_only_test, unique_ptr) {
  small_fifo<std::unique_ptr<int>, 2> f;

  for (int i = 0; i < 6; ++i) {
    f.push_back(std::make_unique<int>(i));
  }

  std::vector<int> values;
  while (!f.empty()) {
    auto p = std::move(f.front());
    f.pop_front();
    ASSERT_NE(nullptr, p);
    values.push_back(*p);
  }

  EXPECT_THAT(values, ElementsAre(0, 1, 2, 3, 4, 5));
}

TEST(small_fifo_move_only_test, futures) {
  small_fifo<std::future<int>, 2> f;

  for (int i = 0; i < 6; ++i) {
    std::promise<int> p;
    f.push_back(p.get_future());
    p.set_value(i);
  }

  std::vector<int> values;
  while (!f.empty()) {
    auto fut = std::move(f.front());
    f.pop_front();
    ASSERT_TRUE(fut.valid());
    values.push_back(fut.get());
  }

  EXPECT_THAT(values, ElementsAre(0, 1, 2, 3, 4, 5));
}

TEST(small_fifo_move_only_test, append_vector_of_futures) {
  small_fifo<std::future<int>, 2> f;

  std::vector<std::future<int>> batch;
  for (int i = 0; i < 4; ++i) {
    std::promise<int> p;
    batch.push_back(p.get_future());
    p.set_value(i);
  }

  f.append(std::move(batch));

  ASSERT_EQ(4u, f.size());

  std::vector<int> values;
  while (!f.empty()) {
    auto fut = std::move(f.front());
    f.pop_front();
    values.push_back(fut.get());
  }

  EXPECT_THAT(values, ElementsAre(0, 1, 2, 3));
}

// --- lifetime --------------------------------------------------------------

TEST_F(small_fifo_tracked_test, destructor_destroys_remaining_elements) {
  {
    small_fifo<tracked, 2> f;
    for (int i = 0; i < 5; ++i) {
      f.emplace_back(i);
    }
    EXPECT_EQ(5, stats().live);
  }
  EXPECT_EQ(0, stats().live);
}

TEST_F(small_fifo_tracked_test, popped_elements_do_not_accumulate) {
  constexpr size_t kInline = 4;
  constexpr int kSteadyDepth = 8;
  constexpr int kIterations = 10'000;

  {
    small_fifo<tracked, kInline> f;

    for (int i = 0; i < kSteadyDepth; ++i) {
      f.emplace_back(i);
    }

    int high_water = stats().live;

    // Steady state: one in, one out. The queue never drains, so the only
    // thing keeping dead slots in check is compaction.
    for (int i = 0; i < kIterations; ++i) {
      f.emplace_back(i);
      f.pop_front();
      high_water = std::max(high_water, stats().live);
    }

    EXPECT_EQ(static_cast<size_t>(kSteadyDepth), f.size());
    EXPECT_LT(high_water, 64)
        << "dead slots are accumulating; compaction is not firing";
    EXPECT_LT(f.capacity(), 128u)
        << "storage grew with iteration count rather than queue depth";
  }

  EXPECT_EQ(0, stats().live);
}

TEST_F(small_fifo_tracked_test, drain_refill_releases_everything) {
  {
    small_fifo<tracked, 2> f;

    for (int round = 0; round < 50; ++round) {
      for (int i = 0; i < 4; ++i) {
        f.emplace_back(i);
      }

      while (!f.empty()) {
        f.pop_front();
      }

      // A fully drained fifo must hold nothing at all.
      EXPECT_EQ(0, stats().live) << "round " << round;
    }
  }

  EXPECT_EQ(0, stats().live);
}

TEST_F(small_fifo_tracked_test, clear_destroys_elements) {
  {
    small_fifo<tracked, 2> f;
    for (int i = 0; i < 6; ++i) {
      f.emplace_back(i);
    }
    f.pop_front();
    ASSERT_EQ(6, stats().live);

    f.clear();
    EXPECT_EQ(0, stats().live);
    EXPECT_TRUE(f.empty());
  }
}

// --- growth beyond inline capacity -----------------------------------------

TEST(small_fifo_growth_test, large_queue_stays_ordered) {
  constexpr int kCount = 100'000;
  small_fifo<int, 4> f;

  for (int i = 0; i < kCount; ++i) {
    f.push_back(i);
  }

  ASSERT_EQ(static_cast<size_t>(kCount), f.size());

  for (int i = 0; i < kCount; ++i) {
    ASSERT_EQ(i, f.front()) << "at index " << i;
    f.pop_front();
  }

  EXPECT_TRUE(f.empty());
}

TEST(small_fifo_growth_test, compaction_preserves_contents) {
  // Push well past the inline capacity, then pop enough to trigger the
  // compaction branch, and check the remainder survived intact.
  small_fifo<int, 4> f;

  std::vector<int> expected(20);
  std::iota(expected.begin(), expected.end(), 0);

  for (auto v : expected) {
    f.push_back(v);
  }

  for (int i = 0; i < 12; ++i) {
    ASSERT_EQ(i, f.front());
    f.pop_front();
  }

  EXPECT_THAT(drain(f),
              ElementsAreArray(expected.begin() + 12, expected.end()));
}

// --- capacity ---------------------------------------------------------------

TEST(small_fifo_capacity_test, inline_capacity_is_exposed) {
  using fifo = small_fifo<int, 8>;
  static_assert(fifo::inline_capacity == 8);

  fifo f;
  EXPECT_GE(f.capacity(), fifo::inline_capacity);
}

TEST(small_fifo_capacity_test, no_allocation_up_to_inline_capacity) {
  small_fifo<int, 8> f;
  auto const initial = f.capacity();

  for (size_t i = 0; i < initial; ++i) {
    f.push_back(static_cast<int>(i));
  }

  EXPECT_EQ(initial, f.capacity()) << "grew before exhausting inline storage";
  EXPECT_EQ(initial, f.size());
}

TEST(small_fifo_capacity_test, grows_past_inline_capacity) {
  small_fifo<int, 4> f;
  auto const initial = f.capacity();

  for (int i = 0; i < 100; ++i) {
    f.push_back(i);
  }

  EXPECT_GT(f.capacity(), initial);
  EXPECT_GE(f.capacity(), f.size());
}

TEST(small_fifo_capacity_test, drain_refill_reuses_storage) {
  // The property the file_reader relies on: once the queue has grown to its
  // working size, repeatedly filling and draining it must not allocate again.
  small_fifo<int, 4> f;
  size_t stable{0};

  for (int round = 0; round < 100; ++round) {
    for (int i = 0; i < 10; ++i) {
      f.push_back(i);
    }

    while (!f.empty()) {
      f.pop_front();
    }

    ASSERT_TRUE(f.empty());

    if (round == 1) {
      stable = f.capacity();
    } else if (round > 1) {
      ASSERT_EQ(stable, f.capacity()) << "reallocated in round " << round;
    }
  }
}

TEST(small_fifo_capacity_test, steady_state_capacity_is_bounded) {
  constexpr int kSteadyDepth = 8;
  constexpr int kIterations = 10'000;

  small_fifo<int, 4> f;

  for (int i = 0; i < kSteadyDepth; ++i) {
    f.push_back(i);
  }

  auto high_water = f.capacity();

  for (int i = 0; i < kIterations; ++i) {
    f.push_back(i);
    f.pop_front();
    high_water = std::max(high_water, f.capacity());
  }

  EXPECT_EQ(static_cast<size_t>(kSteadyDepth), f.size());
  EXPECT_LT(high_water, 128u)
      << "capacity scales with iteration count, not queue depth";
}

TEST(small_fifo_capacity_test, clear_keeps_storage) {
  small_fifo<int, 2> f;

  for (int i = 0; i < 50; ++i) {
    f.push_back(i);
  }

  auto const grown = f.capacity();
  f.clear();

  EXPECT_TRUE(f.empty());
  EXPECT_EQ(grown, f.capacity()) << "clear() should not release storage";
}
