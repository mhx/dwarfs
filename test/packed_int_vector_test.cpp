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

#include <dwarfs/container/packed_value_traits_optional.h>

#include "packed_int_vector_test_helpers.h"

using namespace dwarfs::test;
using namespace dwarfs::container;
using dwarfs::from_range;
using ::testing::Each;
using ::testing::ElementsAre;
using ::testing::ElementsAreArray;
using ::testing::Ge;

namespace {

static_assert(packed_int_vector<uint32_t>::max_size() ==
              std::numeric_limits<std::size_t>::max());
static_assert(compact_packed_int_vector<uint32_t>::max_size() ==
              std::numeric_limits<std::size_t>::max());
static_assert(segmented_packed_int_vector<uint32_t>::max_size() ==
              std::numeric_limits<std::size_t>::max());

static_assert(sizeof(packed_int_vector<uint32_t>) == 2 * sizeof(std::size_t));
static_assert(sizeof(compact_packed_int_vector<uint32_t>) ==
              2 * sizeof(std::size_t));

using segmented_vec = segmented_packed_int_vector<uint32_t, 4>;

} // namespace

TEST(segmented_packed_int_vector, basic_push_back_and_indexing) {
  segmented_vec vec;

  vec.push_back(1);
  vec.push_back(31);
  vec.push_back(0);
  vec.push_back(5);
  vec.push_back(3);
  vec.push_back(25);

  EXPECT_EQ(vec.size(), 6);
  EXPECT_EQ(vec.segment_count(), 2);
  EXPECT_EQ(vec.size_in_bytes(), 8);

  EXPECT_EQ(vec[0], 1);
  EXPECT_EQ(vec[1], 31);
  EXPECT_EQ(vec[2], 0);
  EXPECT_EQ(vec[3], 5);
  EXPECT_EQ(vec[4], 3);
  EXPECT_EQ(vec[5], 25);

  vec[0] = 11;
  vec.at(5) = 7;

  EXPECT_THAT(vec, ElementsAre(11, 31, 0, 5, 3, 7));

  EXPECT_THROW(vec.at(6), std::out_of_range);
  EXPECT_THROW(vec.at(6) = 1, std::out_of_range);

  auto const& cvec = vec;
  EXPECT_EQ(cvec.front(), 11);
  EXPECT_EQ(cvec.back(), 7);
  EXPECT_EQ(cvec.at(1), 31);
  EXPECT_EQ(cvec.at(5), 7);
  EXPECT_THROW(cvec.at(6), std::out_of_range);
}

TEST(segmented_packed_int_vector, resize_across_segment_boundaries) {
  segmented_vec vec;

  for (uint32_t v : {1, 2, 3, 4, 5, 6}) {
    vec.push_back(v);
  }

  vec.resize(10, 17);

  EXPECT_EQ(vec.size(), 10);
  EXPECT_EQ(vec.segment_count(), 3);
  EXPECT_THAT(vec, ElementsAre(1, 2, 3, 4, 5, 6, 17, 17, 17, 17));

  vec.resize(5);

  EXPECT_EQ(vec.size(), 5);
  EXPECT_EQ(vec.segment_count(), 2);
  EXPECT_THAT(vec, ElementsAre(1, 2, 3, 4, 5));

  vec.resize(0);

  EXPECT_EQ(vec.size(), 0);
  EXPECT_EQ(vec.segment_count(), 0);
  EXPECT_TRUE(vec.empty());
}

TEST(segmented_packed_int_vector, pop_back_removes_empty_last_segment) {
  segmented_vec vec;

  for (uint32_t v = 1; v <= 9; ++v) {
    vec.push_back(v);
  }

  ASSERT_EQ(vec.size(), 9);
  ASSERT_EQ(vec.segment_count(), 3);

  vec.pop_back();

  EXPECT_EQ(vec.size(), 8);
  EXPECT_EQ(vec.segment_count(), 2);
  EXPECT_THAT(vec, ElementsAre(1, 2, 3, 4, 5, 6, 7, 8));

  vec.pop_back();
  EXPECT_EQ(vec.size(), 7);
  EXPECT_EQ(vec.segment_count(), 2);
}

TEST(segmented_packed_int_vector, segment_local_widening_updates_histogram) {
  segmented_vec vec(8, 0);

  ASSERT_EQ(vec.segment_count(), 2);

  auto hist = vec.segment_bits_histogram();
  EXPECT_EQ(hist[0], 2);
  EXPECT_EQ(std::accumulate(hist.begin(), hist.end(), std::size_t{0}), 2);

  vec[1] = 31;

  hist = vec.segment_bits_histogram();
  EXPECT_EQ(hist[0], 1);
  EXPECT_EQ(hist[5], 1);
  EXPECT_THAT(vec, ElementsAre(0, 31, 0, 0, 0, 0, 0, 0));

  vec[6] = 3;

  hist = vec.segment_bits_histogram();
  EXPECT_EQ(hist[0], 0);
  EXPECT_EQ(hist[2], 1);
  EXPECT_EQ(hist[5], 1);
  EXPECT_THAT(vec, ElementsAre(0, 31, 0, 0, 0, 0, 3, 0));
}

TEST(segmented_packed_int_vector,
     optimize_storage_shrinks_segments_independently) {
  segmented_vec vec(8, 0);

  vec[1] = 31;
  vec[6] = 3;

  ASSERT_EQ(vec.size_in_bytes(), 8);

  vec[1] = 0;

  auto hist = vec.segment_bits_histogram();
  EXPECT_EQ(hist[2], 1);
  EXPECT_EQ(hist[5], 1);

  vec.optimize_storage();

  hist = vec.segment_bits_histogram();
  EXPECT_EQ(hist[0], 1);
  EXPECT_EQ(hist[2], 1);
  EXPECT_EQ(hist[5], 0);
  EXPECT_EQ(vec.size_in_bytes(), 4);
  EXPECT_THAT(vec, ElementsAre(0, 0, 0, 0, 0, 0, 3, 0));
}

TEST(segmented_packed_int_vector, size_in_bytes_is_sum_of_segment_storage) {
  segmented_vec vec(8, 0);

  EXPECT_EQ(vec.size_in_bytes(), 0);

  vec[0] = 31;
  EXPECT_EQ(vec.size_in_bytes(), 4);

  vec[7] = 31;
  EXPECT_EQ(vec.size_in_bytes(), 8);

  vec[0] = 0;
  vec[7] = 0;
  vec.optimize_storage();

  EXPECT_EQ(vec.size_in_bytes(), 0);
}

TEST(segmented_packed_int_vector, clear_resets_size_and_segments) {
  segmented_vec vec;

  for (uint32_t v = 0; v < 10; ++v) {
    vec.push_back(v);
  }

  ASSERT_EQ(vec.segment_count(), 3);

  vec.clear();

  EXPECT_EQ(vec.size(), 0);
  EXPECT_EQ(vec.segment_count(), 0);
  EXPECT_EQ(vec.size_in_bytes(), 0);
  EXPECT_TRUE(vec.empty());

  vec.push_back(7);
  EXPECT_EQ(vec.size(), 1);
  EXPECT_EQ(vec.segment_count(), 1);
  EXPECT_THAT(vec, ElementsAre(7));
}

TEST(segmented_packed_int_vector, copy_constructor_and_assignment) {
  segmented_vec vec;

  for (uint32_t v : {1, 2, 3, 4, 31, 6}) {
    vec.push_back(v);
  }

  segmented_vec copy{vec};

  EXPECT_EQ(copy.segment_count(), vec.segment_count());
  EXPECT_EQ(copy.size_in_bytes(), vec.size_in_bytes());
  EXPECT_THAT(copy, ElementsAreArray(vec.unpack()));

  copy[4] = 7;

  EXPECT_THAT(vec, ElementsAre(1, 2, 3, 4, 31, 6));
  EXPECT_THAT(copy, ElementsAre(1, 2, 3, 4, 7, 6));

  segmented_vec assigned;
  assigned = vec;

  EXPECT_EQ(assigned.segment_count(), vec.segment_count());
  EXPECT_EQ(assigned.size_in_bytes(), vec.size_in_bytes());
  EXPECT_THAT(assigned, ElementsAreArray(vec.unpack()));
}

TEST(segmented_packed_int_vector, move_constructor_and_assignment) {
  segmented_vec vec;

  for (uint32_t v : {1, 2, 3, 4, 31, 6}) {
    vec.push_back(v);
  }

  auto const expected = vec.unpack();
  auto const expected_segment_count = vec.segment_count();
  auto const expected_size_in_bytes = vec.size_in_bytes();

  segmented_vec moved{std::move(vec)};

  EXPECT_EQ(moved.segment_count(), expected_segment_count);
  EXPECT_EQ(moved.size_in_bytes(), expected_size_in_bytes);
  EXPECT_THAT(moved, ElementsAreArray(expected));

  segmented_vec other;
  other.push_back(99);
  other = std::move(moved);

  EXPECT_EQ(other.segment_count(), expected_segment_count);
  EXPECT_EQ(other.size_in_bytes(), expected_size_in_bytes);
  EXPECT_THAT(other, ElementsAreArray(expected));
}

TEST(segmented_packed_int_vector, supports_custom_types) {
  enum class my_enum : uint8_t { A = 0, B = 1, C = 2, D = 3 };
  using vec_type = segmented_packed_int_vector<my_enum, 4>;

  vec_type vec;
  vec.push_back(my_enum::A);
  vec.push_back(my_enum::C);
  vec.push_back(my_enum::B);
  vec.push_back(my_enum::D);
  vec.push_back(my_enum::A);

  EXPECT_THAT(vec, ElementsAre(my_enum::A, my_enum::C, my_enum::B, my_enum::D,
                               my_enum::A));
}

TEST(compact_packed_int_vector, zero_bits_can_grow_inline_up_to_limit) {
  using vec_type = compact_packed_int_vector<uint32_t>;
  vec_type vec(0);

  auto const n = vec_type::inline_capacity_for_bits(vec.bits());

  EXPECT_TRUE(vec.is_inline());
  EXPECT_FALSE(vec.uses_heap());

  vec.resize(n);

  EXPECT_EQ(vec.bits(), 0);
  EXPECT_EQ(vec.size(), n);
  EXPECT_TRUE(vec.is_inline());
  EXPECT_FALSE(vec.uses_heap());
  EXPECT_THAT(vec, Each(0));

  vec.push_back(0);

  EXPECT_EQ(vec.bits(), 0);
  EXPECT_EQ(vec.size(), n + 1);
  EXPECT_FALSE(vec.is_inline());
  EXPECT_TRUE(vec.uses_heap()); // now used to store size
  EXPECT_THAT(vec, Each(0));

  vec.resize(vec_type::max_size());

  EXPECT_EQ(vec.bits(), 0);
  EXPECT_EQ(vec.size(), vec_type::max_size());
  EXPECT_FALSE(vec.is_inline());
  EXPECT_TRUE(vec.uses_heap());
  // don't unpack :-)
}

TEST(compact_packed_int_vector,
     grows_from_inline_to_heap_when_size_exceeds_inline_capacity) {
  using vec_type = compact_packed_int_vector<uint32_t>;
  vec_type vec(1);

  std::size_t n = 0;
  while (n < vec.capacity()) {
    vec.push_back(1);
    ++n;
  }

  auto const before = vec.unpack();

  vec.push_back(1);

  EXPECT_EQ(vec.size(), before.size() + 1);

  auto const after = vec.unpack();

  EXPECT_THAT(std::span(after).first(before.size()), ElementsAreArray(before));
}

TEST(compact_auto_packed_int_vector,
     grows_from_inline_to_heap_when_bit_width_increases) {
  using vec_type = compact_auto_packed_int_vector<uint32_t>;
  vec_type vec(0);

  for (std::size_t i = 0; i < 8; ++i) {
    vec.push_back(1);
  }

  auto const before = vec.unpack();
  vec.push_back(std::numeric_limits<uint32_t>::max());

  EXPECT_EQ(vec.back(), std::numeric_limits<uint32_t>::max());

  auto const after = vec.unpack();

  EXPECT_THAT(std::span(after).first(before.size()), ElementsAreArray(before));
}

TEST(compact_auto_packed_int_vector,
     optimize_storage_can_move_heap_back_to_inline) {
  using vec_type = compact_auto_packed_int_vector<uint32_t>;
  vec_type vec(0);

  for (std::size_t i = 0; i < 64; ++i) {
    vec.push_back(1023);
  }

  EXPECT_TRUE(vec.uses_heap());
  EXPECT_FALSE(vec.is_inline());

  vec.resize(2);
  vec[0] = 1;
  vec[1] = 1;
  vec.optimize_storage();

  EXPECT_EQ(vec.bits(), 1);
  EXPECT_THAT(vec, ElementsAre(1, 1));
  EXPECT_FALSE(vec.uses_heap());
  EXPECT_TRUE(vec.is_inline());
}

TEST(compact_packed_int_vector, reserve_can_force_heap_representation) {
  using vec_type = compact_packed_int_vector<uint32_t>;
  vec_type vec(4);
  vec.push_back(1);
  vec.push_back(2);

  auto const old = vec.unpack();
  vec.reserve(vec.capacity() + 1);

  EXPECT_THAT(vec, ElementsAreArray(old));
  EXPECT_THAT(vec.capacity(), Ge(old.size()));
}

TEST(compact_packed_int_vector, empty_heap_only_to_compact_copy_fallback) {
  packed_int_vector<uint32_t> src(0); // heap-only, empty, no heap
  compact_packed_int_vector<uint32_t> dst(src);

  EXPECT_TRUE(dst.is_inline());
  EXPECT_FALSE(dst.uses_heap());
  EXPECT_EQ(dst.size(), 0);
  EXPECT_EQ(dst.bits(), 0);
}

TEST(compact_packed_int_vector, assign_from_different_type_as_range) {
  compact_packed_int_vector<uint16_t> vec{1, 2, 3};
  compact_packed_int_vector<int32_t> other{from_range, vec};

  EXPECT_THAT(other, ElementsAre(1, 2, 3));
}

TEST(packed_int_vector_static_api_test, scalar_field_arity_and_max_widths) {
  using vec_type = dwarfs::container::packed_int_vector<uint32_t>;

  EXPECT_EQ(vec_type::field_arity(), 1);
  EXPECT_EQ(vec_type::max_widths(), (typename vec_type::widths_type{32}));
}

TEST(packed_int_vector_static_api_test, tuple_field_arity_and_max_widths) {
  using vec_type = dwarfs::container::packed_int_vector<
      std::tuple<uint16_t, int16_t, uint32_t>>;

  EXPECT_EQ(vec_type::field_arity(), 3);
  EXPECT_EQ(vec_type::max_widths(),
            (typename vec_type::widths_type{16, 16, 32}));
}

TEST(segmented_packed_int_vector, supports_tuple_types) {
  enum class my_enum : uint8_t { A = 0, B = 1, C = 2, D = 3 };
  using value_type = std::tuple<my_enum, uint16_t>;
  using vec_type = segmented_packed_int_vector<value_type, 4>;

  vec_type vec;
  vec.push_back({my_enum::A, 42});
  vec.push_back({my_enum::C, 17});
  vec.push_back({my_enum::B, 99});
  vec.push_back({my_enum::D, 123});
  vec.push_back({my_enum::A, 7});

  EXPECT_THAT(vec, ElementsAre(std::make_tuple(my_enum::A, 42),
                               std::make_tuple(my_enum::C, 17),
                               std::make_tuple(my_enum::B, 99),
                               std::make_tuple(my_enum::D, 123),
                               std::make_tuple(my_enum::A, 7)));
}

TEST(packed_int_vector_proxy_test, optional_value_proxy_has_has_value) {
  using vec_type = compact_auto_packed_int_vector<std::optional<uint32_t>>;
  vec_type vec;

  vec.push_back(std::nullopt);
  vec.push_back(42);

  EXPECT_FALSE(vec[0].has_value());
  EXPECT_TRUE(vec[1].has_value());
  EXPECT_EQ(vec[0], std::nullopt);
  EXPECT_EQ(vec[1], 42);
}

TEST(packed_int_vector_proxy_test, optional_field_proxy_has_has_value) {
  using vec_type = compact_auto_packed_int_vector<
      std::tuple<std::optional<uint32_t>, uint16_t>>;
  vec_type vec;

  vec.push_back({std::nullopt, 42});
  vec.push_back({123, 17});

  EXPECT_FALSE(get<0>(vec[0]).has_value());
  EXPECT_TRUE(get<0>(vec[1]).has_value());
  EXPECT_EQ(get<0>(vec[0]), std::nullopt);
  EXPECT_EQ(get<0>(vec[1]), 123);
}
