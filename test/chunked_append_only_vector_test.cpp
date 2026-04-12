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

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <dwarfs/container/chunked_append_only_vector.h>

using namespace dwarfs::container;
using ::testing::ElementsAre;

namespace {

template <class Vec>
auto snapshot(Vec const& v) {
  std::vector<typename Vec::value_type> out;
  out.reserve(v.size());
  for (std::size_t i = 0; i < v.size(); ++i) {
    out.push_back(v[i]);
  }
  return out;
}

struct move_only {
  explicit move_only(int v)
      : value{v} {}

  move_only(move_only&&) noexcept = default;
  move_only& operator=(move_only&&) noexcept = default;

  move_only(move_only const&) = delete;
  move_only& operator=(move_only const&) = delete;

  int value;
};

struct non_default_constructible {
  non_default_constructible() = delete;

  explicit non_default_constructible(int v)
      : value{v} {}

  int value;
};

struct large_element {
  explicit large_element(int t)
      : tag{t} {}

  std::array<std::byte, 64> payload{};
  int tag;
};

struct tracked {
  static inline int alive = 0;
  static inline int ctor_count = 0;
  static inline int dtor_count = 0;

  static void reset() {
    alive = 0;
    ctor_count = 0;
    dtor_count = 0;
  }

  explicit tracked(int v)
      : value{v} {
    ++alive;
    ++ctor_count;
  }

  tracked(tracked const& other)
      : value{other.value} {
    ++alive;
    ++ctor_count;
  }

  tracked(tracked&& other) noexcept
      : value{other.value} {
    other.value = -1;
    ++alive;
    ++ctor_count;
  }

  tracked& operator=(tracked const&) = default;
  tracked& operator=(tracked&&) noexcept = default;

  ~tracked() {
    --alive;
    ++dtor_count;
  }

  int value;
};

using int_vec_16 = basic_chunked_append_only_vector<int, 16>;
using int_vec_24 = basic_chunked_append_only_vector<int, 24>;
using int_vec_16_pow2 = basic_chunked_append_only_vector<int, 16, true>;
using move_only_vec = basic_chunked_append_only_vector<move_only, 32>;
using tracked_vec = basic_chunked_append_only_vector<tracked, 32>;
using large_vec = basic_chunked_append_only_vector<large_element, 32>;
using pow2_rounding_vec =
    basic_chunked_append_only_vector<std::uint32_t, 24, true>;
using no_rounding_vec =
    basic_chunked_append_only_vector<std::uint32_t, 24, false>;

static_assert(int_vec_16::chunk_elements == 4);
static_assert(int_vec_16::chunk_bytes == 16);

static_assert(pow2_rounding_vec::chunk_elements_raw == 6);
static_assert(pow2_rounding_vec::chunk_elements == 4);
static_assert(pow2_rounding_vec::chunk_bytes == 16);

static_assert(no_rounding_vec::chunk_elements_raw == 6);
static_assert(no_rounding_vec::chunk_elements == 6);
static_assert(no_rounding_vec::chunk_bytes == 24);

static_assert(large_vec::chunk_elements == 1);
static_assert(large_vec::chunk_bytes == sizeof(large_element));

static_assert(std::random_access_iterator<int_vec_16::iterator>);
static_assert(std::random_access_iterator<int_vec_16::const_iterator>);
static_assert(std::sentinel_for<int_vec_16::iterator, int_vec_16::iterator>);
static_assert(
    std::sentinel_for<int_vec_16::const_iterator, int_vec_16::const_iterator>);

static_assert(std::is_same_v<int_vec_16::reference, int&>);
static_assert(std::is_same_v<int_vec_16::const_reference, int const&>);
static_assert(
    std::is_same_v<std::iter_reference_t<int_vec_16::iterator>, int&>);
static_assert(std::is_same_v<std::iter_reference_t<int_vec_16::const_iterator>,
                             int const&>);

TEST(chunked_append_only_vector_test, default_constructed_container_is_empty) {
  int_vec_16 v;

  EXPECT_TRUE(v.empty());
  EXPECT_EQ(0, v.size());
}

TEST(chunked_append_only_vector_test,
     emplace_back_returns_reference_to_inserted_element) {
  int_vec_16 v;

  auto& a = v.emplace_back(10);
  auto& b = v.emplace_back(20);

  EXPECT_EQ(10, a);
  EXPECT_EQ(20, b);

  a = 11;
  b = 21;

  ASSERT_EQ(2, v.size());
  EXPECT_EQ(11, v[0]);
  EXPECT_EQ(21, v[1]);
}

TEST(chunked_append_only_vector_test,
     indexing_and_back_work_within_single_chunk) {
  int_vec_16 v;

  v.emplace_back(1);
  v.emplace_back(2);
  v.emplace_back(3);

  EXPECT_FALSE(v.empty());
  EXPECT_EQ(3, v.size());
  EXPECT_THAT(snapshot(v), ElementsAre(1, 2, 3));
  EXPECT_EQ(1, v.front());
  EXPECT_EQ(3, v.back());

  auto const& cv = v;
  EXPECT_EQ(1, cv.front());
  EXPECT_EQ(2, cv[1]);
  EXPECT_EQ(3, cv.back());
}

TEST(chunked_append_only_vector_test, grows_across_chunk_boundary) {
  int_vec_16 v;

  for (int i = 0; i < 10; ++i) {
    v.emplace_back(100 + i);
  }

  ASSERT_EQ(10, v.size());
  EXPECT_THAT(snapshot(v),
              ElementsAre(100, 101, 102, 103, 104, 105, 106, 107, 108, 109));
}

TEST(chunked_append_only_vector_test,
     references_and_addresses_survive_growth_across_chunks) {
  int_vec_16 v;

  v.emplace_back(1);
  v.emplace_back(2);
  v.emplace_back(3);
  v.emplace_back(4);

  int* p0 = &v[0];
  int* p1 = &v[1];
  int* p2 = &v[2];
  int* p3 = &v[3];

  for (int i = 5; i <= 20; ++i) {
    v.emplace_back(i);
  }

  EXPECT_EQ(p0, &v[0]);
  EXPECT_EQ(p1, &v[1]);
  EXPECT_EQ(p2, &v[2]);
  EXPECT_EQ(p3, &v[3]);

  EXPECT_EQ(1, *p0);
  EXPECT_EQ(2, *p1);
  EXPECT_EQ(3, *p2);
  EXPECT_EQ(4, *p3);
}

TEST(chunked_append_only_vector_test, at_throws_on_out_of_range_access) {
  int_vec_16 v;

  EXPECT_THROW((void)v.at(0), std::out_of_range);

  v.emplace_back(7);
  v.emplace_back(8);

  EXPECT_NO_THROW(EXPECT_EQ(7, v.at(0)));
  EXPECT_NO_THROW(EXPECT_EQ(8, v.at(1)));
  EXPECT_THROW((void)v.at(2), std::out_of_range);

  auto const& cv = v;
  EXPECT_NO_THROW(EXPECT_EQ(7, cv.at(0)));
  EXPECT_THROW((void)cv.at(2), std::out_of_range);
}

TEST(chunked_append_only_vector_test, front_returns_first_inserted_element) {
  int_vec_16 v;

  v.emplace_back(1);
  EXPECT_EQ(1, v.front());

  v.emplace_back(2);
  EXPECT_EQ(1, v.front());

  v.front() = 99;
  EXPECT_EQ(99, v.front());

  auto const& cv = v;
  EXPECT_EQ(99, cv.front());
}

TEST(chunked_append_only_vector_test, back_returns_last_inserted_element) {
  int_vec_16 v;

  v.emplace_back(1);
  EXPECT_EQ(1, v.back());

  v.emplace_back(2);
  EXPECT_EQ(2, v.back());

  v.back() = 99;
  EXPECT_EQ(99, v.back());

  auto const& cv = v;
  EXPECT_EQ(99, cv.back());
}

TEST(chunked_append_only_vector_test, supports_move_only_types) {
  move_only_vec v;

  auto& a = v.emplace_back(10);
  auto& b = v.emplace_back(20);
  auto& c = v.emplace_back(30);

  EXPECT_EQ(10, a.value);
  EXPECT_EQ(20, b.value);
  EXPECT_EQ(30, c.value);

  EXPECT_EQ(10, v[0].value);
  EXPECT_EQ(20, v[1].value);
  EXPECT_EQ(30, v[2].value);
}

TEST(chunked_append_only_vector_test,
     supports_non_default_constructible_types) {
  basic_chunked_append_only_vector<non_default_constructible, 32> v;

  v.emplace_back(3);
  v.emplace_back(5);

  ASSERT_EQ(2, v.size());
  EXPECT_EQ(3, v[0].value);
  EXPECT_EQ(5, v[1].value);
}

TEST(chunked_append_only_vector_test,
     uses_one_element_per_chunk_when_max_chunk_bytes_is_small) {
  large_vec v;

  v.emplace_back(1);
  v.emplace_back(2);
  v.emplace_back(3);

  ASSERT_EQ(3, v.size());
  EXPECT_EQ(1, v[0].tag);
  EXPECT_EQ(2, v[1].tag);
  EXPECT_EQ(3, v[2].tag);

  EXPECT_NE(&v[0], &v[1]);
  EXPECT_NE(&v[1], &v[2]);
}

TEST(chunked_append_only_vector_test,
     power_of_two_chunk_rounding_uses_bit_floor) {
  pow2_rounding_vec v;

  for (std::uint32_t i = 0; i < 9; ++i) {
    v.emplace_back(static_cast<int>(i));
  }

  ASSERT_EQ(9, v.size());
  EXPECT_EQ(0, v[0]);
  EXPECT_EQ(3, v[3]);
  EXPECT_EQ(4, v[4]);
  EXPECT_EQ(8, v[8]);
}

TEST(chunked_append_only_vector_test,
     no_rounding_keeps_raw_chunk_element_count) {
  no_rounding_vec v;

  for (std::uint32_t i = 0; i < 13; ++i) {
    v.emplace_back(static_cast<int>(i));
  }

  ASSERT_EQ(13, v.size());
  EXPECT_EQ(0, v[0]);
  EXPECT_EQ(5, v[5]);
  EXPECT_EQ(6, v[6]);
  EXPECT_EQ(12, v[12]);
}

TEST(chunked_append_only_vector_test,
     clear_destroys_all_constructed_elements_and_resets_state) {
  tracked::reset();

  tracked_vec v;
  v.emplace_back(1);
  v.emplace_back(2);
  v.emplace_back(3);
  v.emplace_back(4);
  v.emplace_back(5);

  EXPECT_EQ(5, tracked::alive);
  EXPECT_EQ(5, tracked::ctor_count);
  EXPECT_EQ(0, tracked::dtor_count);

  v.clear();

  EXPECT_TRUE(v.empty());
  EXPECT_EQ(0, v.size());
  EXPECT_EQ(0, tracked::alive);
  EXPECT_EQ(5, tracked::dtor_count);
}

TEST(chunked_append_only_vector_test,
     clear_allows_reuse_after_destroying_existing_elements) {
  tracked::reset();

  tracked_vec v;
  v.emplace_back(1);
  v.emplace_back(2);
  v.clear();

  EXPECT_TRUE(v.empty());
  EXPECT_EQ(0, tracked::alive);
  EXPECT_EQ(2, tracked::dtor_count);

  v.emplace_back(7);
  v.emplace_back(8);

  ASSERT_EQ(2, v.size());
  EXPECT_EQ(7, v[0].value);
  EXPECT_EQ(8, v[1].value);
  EXPECT_EQ(2, tracked::alive);
}

TEST(chunked_append_only_vector_test,
     destructor_destroys_all_constructed_elements) {
  tracked::reset();

  {
    tracked_vec v;
    v.emplace_back(1);
    v.emplace_back(2);
    v.emplace_back(3);

    EXPECT_EQ(3, tracked::alive);
    EXPECT_EQ(0, tracked::dtor_count);
  }

  EXPECT_EQ(0, tracked::alive);
  EXPECT_EQ(3, tracked::dtor_count);
}

TEST(chunked_append_only_vector_test,
     destruction_after_multiple_chunks_destroys_all_elements) {
  tracked::reset();

  {
    tracked_vec v;
    for (int i = 0; i < 25; ++i) {
      v.emplace_back(i);
    }
    EXPECT_EQ(25, tracked::alive);
  }

  EXPECT_EQ(0, tracked::alive);
  EXPECT_EQ(25, tracked::dtor_count);
}

TEST(chunked_append_only_vector_test,
     move_constructor_preserves_contents_in_destination) {
  int_vec_16 src;
  for (int i = 0; i < 10; ++i) {
    src.emplace_back(i * 10);
  }

  int_vec_16 dst(std::move(src));

  ASSERT_EQ(10, dst.size());
  EXPECT_THAT(snapshot(dst),
              ElementsAre(0, 10, 20, 30, 40, 50, 60, 70, 80, 90));
}

TEST(chunked_append_only_vector_test, move_constructor_leaves_source_empty) {
  int_vec_16 src;
  src.emplace_back(1);
  src.emplace_back(2);

  int_vec_16 dst(std::move(src));

  (void)dst;
  EXPECT_TRUE(src.empty());
  EXPECT_EQ(0, src.size());
}

TEST(chunked_append_only_vector_test,
     move_assignment_destroys_old_destination_elements) {
  tracked::reset();

  {
    tracked_vec src;
    src.emplace_back(10);
    src.emplace_back(20);
    src.emplace_back(30);

    tracked_vec dst;
    dst.emplace_back(1);
    dst.emplace_back(2);

    EXPECT_EQ(5, tracked::alive);

    dst = std::move(src);

    EXPECT_EQ(3, dst.size());
    EXPECT_EQ(10, dst[0].value);
    EXPECT_EQ(20, dst[1].value);
    EXPECT_EQ(30, dst[2].value);

    // If move assignment is correct, the old destination elements were
    // destroyed.
    EXPECT_EQ(3, tracked::alive);
  }

  EXPECT_EQ(0, tracked::alive);
}

TEST(chunked_append_only_vector_test, move_assignment_leaves_source_empty) {
  int_vec_16 src;
  src.emplace_back(1);
  src.emplace_back(2);

  int_vec_16 dst;
  dst.emplace_back(9);

  dst = std::move(src);

  EXPECT_TRUE(src.empty());
  EXPECT_EQ(0, src.size());

  ASSERT_EQ(2, dst.size());
  EXPECT_EQ(1, dst[0]);
  EXPECT_EQ(2, dst[1]);
}

TEST(chunked_append_only_vector_test,
     moved_to_container_can_continue_appending) {
  int_vec_16 src;
  src.emplace_back(1);
  src.emplace_back(2);

  int_vec_16 dst(std::move(src));
  dst.emplace_back(3);
  dst.emplace_back(4);

  EXPECT_THAT(snapshot(dst), ElementsAre(1, 2, 3, 4));
}

TEST(chunked_append_only_vector_test, begin_equals_end_for_empty_container) {
  int_vec_16 v;

  EXPECT_EQ(v.begin(), v.end());
  EXPECT_EQ(v.cbegin(), v.cend());
  EXPECT_EQ(v.rbegin(), v.rend());

  auto const& cv = v;
  EXPECT_EQ(cv.begin(), cv.end());
  EXPECT_EQ(cv.cbegin(), cv.cend());
  EXPECT_EQ(cv.rbegin(), cv.rend());
  EXPECT_EQ(cv.crbegin(), cv.crend());
}

TEST(chunked_append_only_vector_test,
     iterates_forward_with_non_const_iterator) {
  int_vec_16 v;
  for (int i = 0; i < 10; ++i) {
    v.emplace_back(i);
  }

  std::vector<int> out;
  for (auto it = v.begin(); it != v.end(); ++it) {
    out.push_back(*it);
  }

  EXPECT_THAT(out, ElementsAre(0, 1, 2, 3, 4, 5, 6, 7, 8, 9));
}

TEST(chunked_append_only_vector_test, non_const_iterator_allows_modification) {
  int_vec_16 v;
  for (int i = 0; i < 6; ++i) {
    v.emplace_back(i);
  }

  for (auto it = v.begin(); it != v.end(); ++it) {
    *it += 10;
  }

  EXPECT_THAT(snapshot(v), ElementsAre(10, 11, 12, 13, 14, 15));
}

TEST(chunked_append_only_vector_test, iterates_forward_with_const_iterator) {
  int_vec_16 v;
  for (int i = 0; i < 6; ++i) {
    v.emplace_back(100 + i);
  }

  auto const& cv = v;
  std::vector<int> out;
  for (auto it = cv.begin(); it != cv.end(); ++it) {
    out.push_back(*it);
  }

  EXPECT_THAT(out, ElementsAre(100, 101, 102, 103, 104, 105));
}

TEST(chunked_append_only_vector_test, supports_range_for_iteration) {
  int_vec_16 v;
  for (int i = 1; i <= 5; ++i) {
    v.emplace_back(i);
  }

  int sum = 0;
  for (int x : v) {
    sum += x;
  }

  EXPECT_EQ(15, sum);
}

TEST(chunked_append_only_vector_test,
     iterator_random_access_operations_cross_chunk_boundaries) {
  int_vec_16 v;
  for (int i = 0; i < 10; ++i) {
    v.emplace_back(10 * i);
  }

  auto it = v.begin();

  EXPECT_EQ(0, *it);
  EXPECT_EQ(10, it[1]);
  EXPECT_EQ(30, it[3]);
  EXPECT_EQ(40, *(it + 4));
  EXPECT_EQ(70, *(4 + it + 3));

  it += 5;
  EXPECT_EQ(50, *it);

  it -= 2;
  EXPECT_EQ(30, *it);

  EXPECT_EQ(3, it - v.begin());
  EXPECT_EQ(v.begin() + 10, v.end());
  EXPECT_EQ(10, v.end() - v.begin());
}

TEST(chunked_append_only_vector_test,
     const_iterator_random_access_operations_cross_chunk_boundaries) {
  int_vec_16 v;
  for (int i = 0; i < 10; ++i) {
    v.emplace_back(i);
  }

  auto const& cv = v;
  auto it = cv.cbegin();

  EXPECT_EQ(0, *it);
  EXPECT_EQ(4, *(it + 4));
  EXPECT_EQ(8, it[8]);
  EXPECT_EQ(10, cv.cend() - cv.cbegin());
}

TEST(chunked_append_only_vector_test, iterator_comparisons_work) {
  int_vec_16 v;
  for (int i = 0; i < 8; ++i) {
    v.emplace_back(i);
  }

  auto a = v.begin() + 2;
  auto b = v.begin() + 5;

  EXPECT_TRUE(a < b);
  EXPECT_TRUE(a <= b);
  EXPECT_TRUE(b > a);
  EXPECT_TRUE(b >= a);
  EXPECT_TRUE(a != b);
  EXPECT_EQ(3, b - a);
}

TEST(chunked_append_only_vector_test,
     reverse_iteration_visits_elements_in_reverse_order) {
  int_vec_16 v;
  for (int i = 1; i <= 6; ++i) {
    v.emplace_back(i);
  }

  std::vector<int> out;
  for (auto it = v.rbegin(); it != v.rend(); ++it) {
    out.push_back(*it);
  }

  EXPECT_THAT(out, ElementsAre(6, 5, 4, 3, 2, 1));
}

TEST(chunked_append_only_vector_test,
     const_reverse_iteration_visits_elements_in_reverse_order) {
  int_vec_16 v;
  for (int i = 1; i <= 6; ++i) {
    v.emplace_back(i);
  }

  auto const& cv = v;
  std::vector<int> out;
  for (auto it = cv.crbegin(); it != cv.crend(); ++it) {
    out.push_back(*it);
  }

  EXPECT_THAT(out, ElementsAre(6, 5, 4, 3, 2, 1));
}

TEST(chunked_append_only_vector_test, reverse_iterator_allows_modification) {
  int_vec_16 v;
  for (int i = 1; i <= 4; ++i) {
    v.emplace_back(i);
  }

  for (auto it = v.rbegin(); it != v.rend(); ++it) {
    *it *= 10;
  }

  EXPECT_THAT(snapshot(v), ElementsAre(10, 20, 30, 40));
}

TEST(chunked_append_only_vector_test,
     iterators_to_existing_elements_remain_valid_after_append) {
  int_vec_16 v;
  for (int i = 0; i < 4; ++i) {
    v.emplace_back(100 + i);
  }

  auto it0 = v.begin();
  auto it2 = v.begin() + 2;
  int* p0 = &*it0;
  int* p2 = &*it2;

  for (int i = 0; i < 20; ++i) {
    v.emplace_back(200 + i);
  }

  EXPECT_EQ(100, *it0);
  EXPECT_EQ(102, *it2);
  EXPECT_EQ(p0, &*it0);
  EXPECT_EQ(p2, &*it2);
}

TEST(chunked_append_only_vector_test,
     old_end_iterator_becomes_dereferenceable_after_append) {
  int_vec_16 v;
  v.emplace_back(1);
  v.emplace_back(2);

  auto old_end = v.end();

  v.emplace_back(3);

  EXPECT_EQ(v.begin() + 2, old_end);
  EXPECT_EQ(3, *old_end);
}

TEST(chunked_append_only_vector_test, works_with_ranges_find_and_distance) {
  int_vec_16 v;
  for (int i = 0; i < 10; ++i) {
    v.emplace_back(i * 3);
  }

  auto it = std::ranges::find(v, 12);
  ASSERT_NE(it, v.end());
  EXPECT_EQ(12, *it);
  EXPECT_EQ(4, std::ranges::distance(v.begin(), it));
}

TEST(chunked_append_only_vector_test, works_with_ranges_sort) {
  int_vec_16 v;
  v.emplace_back(5);
  v.emplace_back(1);
  v.emplace_back(4);
  v.emplace_back(2);
  v.emplace_back(3);

  std::ranges::sort(v);

  EXPECT_THAT(snapshot(v), ElementsAre(1, 2, 3, 4, 5));
}

TEST(chunked_append_only_vector_test, works_with_ranges_reverse) {
  int_vec_16 v;
  for (int i = 1; i <= 5; ++i) {
    v.emplace_back(i);
  }

  std::ranges::reverse(v);

  EXPECT_THAT(snapshot(v), ElementsAre(5, 4, 3, 2, 1));
}

TEST(chunked_append_only_vector_test,
     iterator_arrow_operator_returns_element_address) {
  tracked_vec v;
  v.emplace_back(7);
  v.emplace_back(9);

  auto it = v.begin();
  EXPECT_EQ(&v[0], it.operator->());

  auto const& cv = v;
  auto cit = cv.begin();
  EXPECT_EQ(&cv[0], cit.operator->());
}

} // namespace
