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

#ifdef __clang__
#pragma clang optimize off
#endif

#include "packed_int_vector_test_helpers.h"

using namespace dwarfs::test;
using namespace dwarfs::internal;
using ::testing::Each;
using ::testing::ElementsAreArray;

namespace {

template <typename Vec>
void expect_representation_invariants(Vec const& vec) {
  EXPECT_LE(vec.size(), Vec::max_size());
  EXPECT_GE(vec.capacity(), vec.size());

  if constexpr (Vec::has_inline_storage) {
    if (vec.is_inline()) {
      EXPECT_FALSE(vec.uses_heap());
      EXPECT_LE(vec.size(), Vec::inline_capacity_for_bits(vec.bits()));
    } else if (!vec.uses_heap()) {
      // Compact zero-bit “extended capacity” state.
      EXPECT_EQ(vec.bits(), 0);
      EXPECT_GT(vec.capacity(), Vec::inline_capacity_for_bits(0));
    }
  } else {
    EXPECT_FALSE(vec.is_inline());
  }
}

template <typename T>
T cross_type_test_value(std::size_t i) {
  if constexpr (std::is_signed_v<T>) {
    switch (i % 5) {
    case 0:
      return T{0};
    case 1:
      return T{1};
    case 2:
      return T{-1};
    case 3:
      return T{2};
    default:
      return T{-2};
    }
  } else {
    return static_cast<T>(i % 5);
  }
}

template <typename Vec>
std::vector<typename Vec::value_type>
append_cross_type_test_values(Vec& vec, std::size_t count) {
  std::vector<typename Vec::value_type> expected;
  expected.reserve(count);

  for (std::size_t i = 0; i < count; ++i) {
    auto const value = cross_type_test_value<typename Vec::value_type>(i);
    vec.push_back(value);
    expected.push_back(value);
  }

  return expected;
}

template <typename Vec>
void expect_values_and_bits(
    Vec const& vec, std::vector<typename Vec::value_type> const& expected,
    typename Vec::size_type expected_bits) {
  EXPECT_EQ(vec.size(), expected.size());
  EXPECT_EQ(vec.bits(), expected_bits);
  EXPECT_THAT(vec, ::testing::ElementsAreArray(expected));
}

template <typename Vec>
void expect_moved_from_empty(Vec const& vec) {
  EXPECT_EQ(vec.size(), 0);
  EXPECT_EQ(vec.bits(), 0);
  EXPECT_FALSE(vec.uses_heap());
}

template <typename Vec>
using opposite_fixed_vec_t =
    std::conditional_t<Vec::has_inline_storage,
                       packed_int_vector<typename Vec::value_type>,
                       compact_packed_int_vector<typename Vec::value_type>>;

template <typename Vec>
using opposite_auto_vec_t = std::conditional_t<
    Vec::has_inline_storage, auto_packed_int_vector<typename Vec::value_type>,
    compact_auto_packed_int_vector<typename Vec::value_type>>;

template <typename Vec>
std::size_t heap_source_size_for_bits(std::size_t bits) {
  if constexpr (Vec::has_inline_storage) {
    auto const inline_cap = Vec::inline_capacity_for_bits(bits);
    EXPECT_LT(inline_cap, Vec::max_size());
    return inline_cap + 1;
  } else {
    return 8;
  }
}

} // namespace

template <typename Vec>
class packed_int_vec_core_test : public ::testing::Test {};

using packed_vector_type_types =
    ::testing::Types<packed_int_vector_type_selector<uint8_t>,
                     compact_packed_int_vector_type_selector<uint8_t>,
                     packed_int_vector_type_selector<int8_t>,
                     compact_packed_int_vector_type_selector<int8_t>,
                     packed_int_vector_type_selector<uint16_t>,
                     compact_packed_int_vector_type_selector<uint16_t>,
                     packed_int_vector_type_selector<int16_t>,
                     compact_packed_int_vector_type_selector<int16_t>,
                     packed_int_vector_type_selector<uint32_t>,
                     compact_packed_int_vector_type_selector<uint32_t>,
                     packed_int_vector_type_selector<int32_t>,
                     compact_packed_int_vector_type_selector<int32_t>,
                     packed_int_vector_type_selector<uint64_t>,
                     compact_packed_int_vector_type_selector<uint64_t>,
                     packed_int_vector_type_selector<int64_t>,
                     compact_packed_int_vector_type_selector<int64_t>>;

TYPED_TEST_SUITE(packed_int_vec_core_test, packed_vector_type_types);

TYPED_TEST(packed_int_vec_core_test,
           compact_inline_capacity_boundary_uses_expected_representation) {
  using vec_type = typename TypeParam::type;
  using size_type = typename vec_type::size_type;

  if constexpr (vec_type::has_inline_storage) {
    std::array<size_type, 4> const test_bits{
        0,
        1,
        std::min<size_type>(4, vec_type::bits_per_block),
        vec_type::bits_per_block,
    };

    for (auto bits : test_bits) {
      SCOPED_TRACE(::testing::Message() << "bits=" << bits);

      auto const inline_cap = vec_type::inline_capacity_for_bits(bits);

      vec_type at_boundary(bits, inline_cap);
      EXPECT_TRUE(at_boundary.is_inline());
      EXPECT_FALSE(at_boundary.uses_heap());
      EXPECT_EQ(at_boundary.bits(), bits);
      EXPECT_EQ(at_boundary.size(), inline_cap);
      EXPECT_THAT(at_boundary, Each(typename vec_type::value_type{}));
      expect_representation_invariants(at_boundary);

      if (inline_cap < vec_type::max_size()) {
        vec_type over_boundary(bits, inline_cap + 1);
        EXPECT_FALSE(over_boundary.is_inline());
        EXPECT_EQ(over_boundary.bits(), bits);
        EXPECT_EQ(over_boundary.size(), inline_cap + 1);
        EXPECT_TRUE(over_boundary.uses_heap()); // now used to store size

        if (bits == 0) {
          EXPECT_EQ(over_boundary.size_in_bytes(), 0);
        } else {
          EXPECT_GT(over_boundary.size_in_bytes(), 0);
        }

        EXPECT_THAT(over_boundary, Each(typename vec_type::value_type{}));
        expect_representation_invariants(over_boundary);
      }
    }
  }
}

TYPED_TEST(packed_int_vec_core_test,
           compact_zero_bit_reserve_crosses_inline_boundary_without_heap) {
  using vec_type = typename TypeParam::type;

  if constexpr (vec_type::has_inline_storage) {
    vec_type vec(0);

    auto const inline_cap = vec_type::inline_capacity_for_bits(0);
    ASSERT_GT(inline_cap, 0u);

    EXPECT_TRUE(vec.is_inline());
    EXPECT_FALSE(vec.uses_heap());
    EXPECT_EQ(vec.bits(), 0);
    EXPECT_EQ(vec.capacity(), inline_cap);
    expect_representation_invariants(vec);

    vec.reserve(inline_cap);
    EXPECT_TRUE(vec.is_inline());
    EXPECT_FALSE(vec.uses_heap());
    EXPECT_EQ(vec.bits(), 0);
    EXPECT_EQ(vec.size(), 0);
    EXPECT_EQ(vec.size_in_bytes(), 0);
    EXPECT_EQ(vec.capacity(), inline_cap);
    expect_representation_invariants(vec);

    if (inline_cap < vec_type::max_size()) {
      vec.reserve(inline_cap + 1);

      EXPECT_FALSE(vec.is_inline());
      EXPECT_TRUE(vec.uses_heap()); // now used to store size
      EXPECT_EQ(vec.bits(), 0);
      EXPECT_EQ(vec.size(), 0);
      EXPECT_EQ(vec.size_in_bytes(), 0);
      EXPECT_GE(vec.capacity(), inline_cap + 1);
      expect_representation_invariants(vec);
    }
  }
}

TYPED_TEST(packed_int_vec_core_test,
           compact_shrink_to_fit_can_move_long_zero_bit_vector_back_inline) {
  using vec_type = typename TypeParam::type;

  if constexpr (vec_type::has_inline_storage) {
    auto const inline_cap = vec_type::inline_capacity_for_bits(0);
    ASSERT_LT(inline_cap, vec_type::max_size());

    vec_type vec(0, inline_cap + 1);

    EXPECT_FALSE(vec.is_inline());
    EXPECT_TRUE(vec.uses_heap()); // used to store size
    EXPECT_EQ(vec.bits(), 0);
    EXPECT_EQ(vec.size_in_bytes(), 0);
    expect_representation_invariants(vec);

    vec.resize(inline_cap);

    // resize alone should not necessarily change representation
    EXPECT_FALSE(vec.is_inline());
    EXPECT_TRUE(vec.uses_heap()); // still used to store size
    EXPECT_EQ(vec.bits(), 0);
    EXPECT_EQ(vec.size(), inline_cap);
    expect_representation_invariants(vec);

    vec.shrink_to_fit();

    EXPECT_TRUE(vec.is_inline());
    EXPECT_FALSE(vec.uses_heap());
    EXPECT_EQ(vec.bits(), 0);
    EXPECT_EQ(vec.size(), inline_cap);
    EXPECT_EQ(vec.size_in_bytes(), 0);
    EXPECT_THAT(vec, Each(typename vec_type::value_type{}));
    expect_representation_invariants(vec);
  }
}

TYPED_TEST(packed_int_vec_core_test,
           compact_swap_preserves_contents_and_representation_kind) {
  using vec_type = typename TypeParam::type;
  using value_type = typename vec_type::value_type;

  if constexpr (vec_type::has_inline_storage) {
    auto make_inline_vec = [] {
      vec_type vec(2);
      vec.push_back(static_cast<value_type>(1));
      return vec;
    };

    auto make_long_zero_vec = [] {
      auto const zero_inline_cap = vec_type::inline_capacity_for_bits(0);
      assert(zero_inline_cap < vec_type::max_size());
      return vec_type(0, zero_inline_cap + 1);
    };

    auto make_heap_vec = [] {
      auto const full_inline_cap =
          vec_type::inline_capacity_for_bits(vec_type::bits_per_block);
      assert(full_inline_cap < vec_type::max_size());

      vec_type vec(vec_type::bits_per_block, full_inline_cap + 1);
      vec[0] = static_cast<value_type>(1);
      return vec;
    };

    {
      auto a = make_inline_vec();
      auto b = make_heap_vec();

      auto const a_expected = a.unpack();
      auto const b_expected = b.unpack();

      EXPECT_TRUE(a.is_inline());
      EXPECT_FALSE(a.uses_heap());
      EXPECT_FALSE(b.is_inline());
      EXPECT_TRUE(b.uses_heap());

      a.swap(b);

      EXPECT_THAT(a, ElementsAreArray(b_expected));
      EXPECT_THAT(b, ElementsAreArray(a_expected));

      EXPECT_FALSE(a.is_inline());
      EXPECT_TRUE(a.uses_heap());
      EXPECT_TRUE(b.is_inline());
      EXPECT_FALSE(b.uses_heap());

      expect_representation_invariants(a);
      expect_representation_invariants(b);
    }

    {
      auto a = make_inline_vec();
      auto b = make_long_zero_vec();

      auto const a_expected = a.unpack();
      auto const b_expected = b.unpack();

      EXPECT_TRUE(a.is_inline());
      EXPECT_FALSE(a.uses_heap());
      EXPECT_FALSE(b.is_inline());
      EXPECT_TRUE(b.uses_heap()); // used to store size

      a.swap(b);

      EXPECT_THAT(a, ElementsAreArray(b_expected));
      EXPECT_THAT(b, ElementsAreArray(a_expected));

      EXPECT_FALSE(a.is_inline());
      EXPECT_TRUE(a.uses_heap()); // used to store size
      EXPECT_TRUE(b.is_inline());
      EXPECT_FALSE(b.uses_heap());

      expect_representation_invariants(a);
      expect_representation_invariants(b);
    }

    {
      auto a = make_heap_vec();
      auto b = make_long_zero_vec();

      auto const a_expected = a.unpack();
      auto const b_expected = b.unpack();

      EXPECT_FALSE(a.is_inline());
      EXPECT_TRUE(a.uses_heap());
      EXPECT_FALSE(b.is_inline());
      EXPECT_TRUE(b.uses_heap()); // used to store size

      a.swap(b);

      EXPECT_THAT(a, ElementsAreArray(b_expected));
      EXPECT_THAT(b, ElementsAreArray(a_expected));

      EXPECT_FALSE(a.is_inline());
      EXPECT_TRUE(a.uses_heap()); // used to store size
      EXPECT_FALSE(b.is_inline());
      EXPECT_TRUE(b.uses_heap());

      expect_representation_invariants(a);
      expect_representation_invariants(b);
    }
  }
}

TYPED_TEST(packed_int_vec_core_test,
           compact_signed_auto_assignment_can_widen_from_inline_to_heap) {
  using auto_vec_type = typename TypeParam::auto_type;
  using value_type = typename auto_vec_type::value_type;

  if constexpr (auto_vec_type::has_inline_storage &&
                std::is_signed_v<value_type>) {
    auto const target_value = std::numeric_limits<value_type>::max();
    auto const target_bits = auto_vec_type::required_bits(target_value);
    auto const small_bits = std::max<std::size_t>(
        std::size_t{2}, target_bits > 1 ? target_bits - 1 : std::size_t{1});

    auto const small_inline_cap =
        auto_vec_type::inline_capacity_for_bits(small_bits);
    auto const full_inline_cap =
        auto_vec_type::inline_capacity_for_bits(target_bits);

    if (small_inline_cap <= full_inline_cap || small_inline_cap == 0) {
      SUCCEED();
      return;
    }

    auto_vec_type vec(0, small_inline_cap);

    for (std::size_t i = 0; i < vec.size(); ++i) {
      vec[i] = static_cast<value_type>(1);
    }

    ASSERT_EQ(vec.bits(),
              auto_vec_type::required_bits(static_cast<value_type>(1)));
    ASSERT_TRUE(vec.is_inline());
    ASSERT_FALSE(vec.uses_heap());

    vec[vec.size() - 1] = target_value;

    EXPECT_EQ(vec.bits(), target_bits);
    EXPECT_FALSE(vec.is_inline());
    EXPECT_TRUE(vec.uses_heap());
    EXPECT_EQ(vec.back(), target_value);

    for (std::size_t i = 0; i + 1 < vec.size(); ++i) {
      EXPECT_EQ(vec[i], static_cast<value_type>(1));
    }

    expect_representation_invariants(vec);
  }
}

TYPED_TEST(packed_int_vec_core_test,
           representation_invariants_hold_after_targeted_state_changes) {
  using vec_type = typename TypeParam::auto_type;
  using value_type = typename vec_type::value_type;

  vec_type vec(0);
  expect_representation_invariants(vec);

  vec.resize(3);
  expect_representation_invariants(vec);

  vec[0] = static_cast<value_type>(1);
  expect_representation_invariants(vec);

  vec.push_back(static_cast<value_type>(1));
  expect_representation_invariants(vec);

  vec.optimize_storage();
  expect_representation_invariants(vec);

  vec.truncate_to_bits(0);
  expect_representation_invariants(vec);

  if constexpr (vec_type::has_inline_storage) {
    auto const zero_inline_cap = vec_type::inline_capacity_for_bits(0);
    if (zero_inline_cap < vec_type::max_size()) {
      vec.resize(zero_inline_cap + 1);
      expect_representation_invariants(vec);

      vec.shrink_to_fit();
      expect_representation_invariants(vec);

      vec.resize(zero_inline_cap);
      vec.shrink_to_fit();
      expect_representation_invariants(vec);
    }
  }
}

TYPED_TEST(packed_int_vec_core_test,
           push_back_at_max_size_throws_and_preserves_state) {
  using vec_type = typename TypeParam::type;
  using value_type = typename vec_type::value_type;

  vec_type vec(0, vec_type::max_size());

  ASSERT_EQ(vec.size(), vec_type::max_size());
  ASSERT_EQ(vec.bits(), 0);

  auto const was_inline = vec.is_inline();
  auto const used_heap = vec.uses_heap();

  EXPECT_THROW(vec.push_back(value_type{}), std::length_error);

  EXPECT_EQ(vec.size(), vec_type::max_size());
  EXPECT_EQ(vec.bits(), 0);
  EXPECT_EQ(vec.is_inline(), was_inline);
  EXPECT_EQ(vec.uses_heap(), used_heap);
}

TYPED_TEST(packed_int_vec_core_test,
           auto_push_back_at_max_size_throws_and_preserves_state) {
  using vec_type = typename TypeParam::auto_type;
  using value_type = typename vec_type::value_type;

  vec_type vec(0, vec_type::max_size());

  ASSERT_EQ(vec.size(), vec_type::max_size());
  ASSERT_EQ(vec.bits(), 0);

  auto const was_inline = vec.is_inline();
  auto const used_heap = vec.uses_heap();

  EXPECT_THROW(vec.push_back(value_type{}), std::length_error);

  EXPECT_EQ(vec.size(), vec_type::max_size());
  EXPECT_EQ(vec.bits(), 0);
  EXPECT_EQ(vec.is_inline(), was_inline);
  EXPECT_EQ(vec.uses_heap(), used_heap);
}

TYPED_TEST(
    packed_int_vec_core_test,
    cross_strategy_copy_construct_preserves_bits_and_values_and_inline_when_available) {
  using fixed_vec = typename TypeParam::type;
  using auto_vec = typename TypeParam::auto_type;
  using size_type = typename fixed_vec::size_type;

  auto const bits =
      std::min<size_type>(size_type{3}, fixed_vec::bits_per_block);
  auto const count = [&] {
    if constexpr (fixed_vec::has_inline_storage) {
      return std::min<size_type>(fixed_vec::inline_capacity_for_bits(bits),
                                 size_type{5});
    } else {
      return size_type{5};
    }
  }();

  auto_vec src(bits);
  auto const expected = append_cross_type_test_values(src, count);

  ASSERT_EQ(src.bits(), bits);
  if constexpr (fixed_vec::has_inline_storage) {
    ASSERT_TRUE(src.is_inline());
    ASSERT_FALSE(src.uses_heap());
  } else {
    ASSERT_FALSE(src.is_inline());
    ASSERT_TRUE(src.uses_heap());
  }

  fixed_vec dst(src);

  expect_values_and_bits(dst, expected, bits);

  if constexpr (fixed_vec::has_inline_storage) {
    EXPECT_TRUE(dst.is_inline());
    EXPECT_FALSE(dst.uses_heap());
  } else {
    EXPECT_FALSE(dst.is_inline());
    EXPECT_TRUE(dst.uses_heap());
  }
}

TYPED_TEST(
    packed_int_vec_core_test,
    cross_strategy_copy_assignment_overwrites_existing_contents_and_preserves_bits) {
  using fixed_vec = typename TypeParam::type;
  using auto_vec = typename TypeParam::auto_type;
  using size_type = typename fixed_vec::size_type;

  auto const bits =
      std::min<size_type>(size_type{3}, fixed_vec::bits_per_block);
  auto const count = [&] {
    if constexpr (fixed_vec::has_inline_storage) {
      return std::min<size_type>(fixed_vec::inline_capacity_for_bits(bits),
                                 size_type{5});
    } else {
      return size_type{5};
    }
  }();

  fixed_vec src(bits);
  auto const expected = append_cross_type_test_values(src, count);

  auto_vec dst(1);
  dst.push_back(cross_type_test_value<typename auto_vec::value_type>(4));
  dst.push_back(cross_type_test_value<typename auto_vec::value_type>(3));

  dst = src;

  expect_values_and_bits(dst, expected, bits);

  if constexpr (auto_vec::has_inline_storage) {
    EXPECT_TRUE(dst.is_inline());
    EXPECT_FALSE(dst.uses_heap());
  } else {
    EXPECT_FALSE(dst.is_inline());
    EXPECT_TRUE(dst.uses_heap());
  }
}

TYPED_TEST(
    packed_int_vec_core_test,
    cross_policy_move_construct_from_heap_backed_source_preserves_bits_and_values) {
  using dst_vec = typename TypeParam::type;
  using src_vec = opposite_fixed_vec_t<dst_vec>;
  using size_type = typename dst_vec::size_type;

  auto const bits = std::min<size_type>(size_type{3}, dst_vec::bits_per_block);
  auto const count = heap_source_size_for_bits<src_vec>(bits);

  src_vec src(bits);
  auto const expected = append_cross_type_test_values(src, count);

  ASSERT_EQ(src.bits(), bits);
  ASSERT_TRUE(src.uses_heap());
  if constexpr (src_vec::has_inline_storage) {
    ASSERT_FALSE(src.is_inline());
  }

  dst_vec dst(std::move(src));

  expect_values_and_bits(dst, expected, bits);
  expect_moved_from_empty(src);
}

TYPED_TEST(
    packed_int_vec_core_test,
    cross_policy_move_assignment_from_heap_backed_source_overwrites_existing_contents) {
  using dst_vec = typename TypeParam::auto_type;
  using src_vec = opposite_auto_vec_t<dst_vec>;
  using size_type = typename dst_vec::size_type;

  auto const bits = std::min<size_type>(size_type{3}, dst_vec::bits_per_block);
  auto const count = heap_source_size_for_bits<src_vec>(bits);

  src_vec src(bits);
  auto const expected = append_cross_type_test_values(src, count);

  ASSERT_EQ(src.bits(), bits);
  ASSERT_TRUE(src.uses_heap());
  if constexpr (src_vec::has_inline_storage) {
    ASSERT_FALSE(src.is_inline());
  }

  dst_vec dst(1);
  dst.push_back(cross_type_test_value<typename dst_vec::value_type>(1));
  dst.push_back(cross_type_test_value<typename dst_vec::value_type>(2));

  dst = std::move(src);

  expect_values_and_bits(dst, expected, bits);
  expect_moved_from_empty(src);
}

TYPED_TEST(
    packed_int_vec_core_test,
    compact_inline_source_to_heap_only_copy_construction_uses_heap_fallback) {
  using compact_vec = std::conditional_t<
      TypeParam::type::has_inline_storage, typename TypeParam::type,
      compact_packed_int_vector<typename TypeParam::type::value_type>>;
  using heap_vec = packed_int_vector<typename TypeParam::type::value_type>;
  using value_type = typename compact_vec::value_type;
  using size_type = typename compact_vec::size_type;

  auto const bits =
      std::min<size_type>(size_type{3}, compact_vec::bits_per_block);
  auto const count = std::min<size_type>(
      compact_vec::inline_capacity_for_bits(bits), size_type{5});

  ASSERT_GT(count, 0u);

  compact_vec src(bits);
  std::vector<value_type> expected;
  expected.reserve(count);

  for (size_type i = 0; i < count; ++i) {
    auto const v = cross_type_test_value<value_type>(i);
    src.push_back(v);
    expected.push_back(v);
  }

  ASSERT_TRUE(src.is_inline());
  ASSERT_FALSE(src.uses_heap());
  ASSERT_EQ(src.bits(), bits);

  heap_vec dst(src);

  EXPECT_FALSE(dst.is_inline());
  EXPECT_TRUE(dst.uses_heap());
  EXPECT_EQ(dst.bits(), bits);
  EXPECT_THAT(dst, ElementsAreArray(expected));
}

TYPED_TEST(
    packed_int_vec_core_test,
    compact_inline_source_to_heap_only_copy_assignment_uses_heap_fallback) {
  using compact_vec = std::conditional_t<
      TypeParam::type::has_inline_storage, typename TypeParam::type,
      compact_packed_int_vector<typename TypeParam::type::value_type>>;
  using heap_vec = packed_int_vector<typename TypeParam::type::value_type>;
  using value_type = typename compact_vec::value_type;
  using size_type = typename compact_vec::size_type;

  auto const bits =
      std::min<size_type>(size_type{3}, compact_vec::bits_per_block);
  auto const count = std::min<size_type>(
      compact_vec::inline_capacity_for_bits(bits), size_type{5});

  ASSERT_GT(count, 0u);

  compact_vec src(bits);
  std::vector<value_type> expected;
  expected.reserve(count);

  for (size_type i = 0; i < count; ++i) {
    auto const v = cross_type_test_value<value_type>(i);
    src.push_back(v);
    expected.push_back(v);
  }

  ASSERT_TRUE(src.is_inline());
  ASSERT_FALSE(src.uses_heap());
  ASSERT_EQ(src.bits(), bits);

  heap_vec dst(1);
  dst.push_back(cross_type_test_value<value_type>(7));
  dst.push_back(cross_type_test_value<value_type>(8));

  dst = src;

  EXPECT_FALSE(dst.is_inline());
  EXPECT_TRUE(dst.uses_heap());
  EXPECT_EQ(dst.bits(), bits);
  EXPECT_THAT(dst, ElementsAreArray(expected));

  // source must stay unchanged for copy assignment
  EXPECT_TRUE(src.is_inline());
  EXPECT_FALSE(src.uses_heap());
  EXPECT_EQ(src.bits(), bits);
  EXPECT_THAT(src, ElementsAreArray(expected));
}

TYPED_TEST(
    packed_int_vec_core_test,
    compact_inline_auto_source_to_heap_only_auto_copy_construction_uses_heap_fallback) {
  using compact_vec =
      std::conditional_t<TypeParam::auto_type::has_inline_storage,
                         typename TypeParam::auto_type,
                         compact_auto_packed_int_vector<
                             typename TypeParam::auto_type::value_type>>;
  using heap_vec =
      auto_packed_int_vector<typename TypeParam::auto_type::value_type>;
  using value_type = typename compact_vec::value_type;
  using size_type = typename compact_vec::size_type;

  auto const bits =
      std::min<size_type>(size_type{3}, compact_vec::bits_per_block);
  auto const count = std::min<size_type>(
      compact_vec::inline_capacity_for_bits(bits), size_type{5});

  ASSERT_GT(count, 0u);

  compact_vec src(bits);
  std::vector<value_type> expected;
  expected.reserve(count);

  for (size_type i = 0; i < count; ++i) {
    auto const v = cross_type_test_value<value_type>(i);
    src.push_back(v);
    expected.push_back(v);
  }

  ASSERT_TRUE(src.is_inline());
  ASSERT_FALSE(src.uses_heap());
  ASSERT_EQ(src.bits(), bits);

  heap_vec dst(src);

  EXPECT_FALSE(dst.is_inline());
  EXPECT_TRUE(dst.uses_heap());
  EXPECT_EQ(dst.bits(), bits);
  EXPECT_THAT(dst, ElementsAreArray(expected));
}

TYPED_TEST(
    packed_int_vec_core_test,
    compact_inline_auto_source_to_heap_only_auto_copy_assignment_uses_heap_fallback) {
  using compact_vec =
      std::conditional_t<TypeParam::auto_type::has_inline_storage,
                         typename TypeParam::auto_type,
                         compact_auto_packed_int_vector<
                             typename TypeParam::auto_type::value_type>>;
  using heap_vec =
      auto_packed_int_vector<typename TypeParam::auto_type::value_type>;
  using value_type = typename compact_vec::value_type;
  using size_type = typename compact_vec::size_type;

  auto const bits =
      std::min<size_type>(size_type{3}, compact_vec::bits_per_block);
  auto const count = std::min<size_type>(
      compact_vec::inline_capacity_for_bits(bits), size_type{5});

  ASSERT_GT(count, 0u);

  compact_vec src(bits);
  std::vector<value_type> expected;
  expected.reserve(count);

  for (size_type i = 0; i < count; ++i) {
    auto const v = cross_type_test_value<value_type>(i);
    src.push_back(v);
    expected.push_back(v);
  }

  ASSERT_TRUE(src.is_inline());
  ASSERT_FALSE(src.uses_heap());
  ASSERT_EQ(src.bits(), bits);

  heap_vec dst(1);
  dst.push_back(cross_type_test_value<value_type>(7));
  dst.push_back(cross_type_test_value<value_type>(8));

  dst = src;

  EXPECT_FALSE(dst.is_inline());
  EXPECT_TRUE(dst.uses_heap());
  EXPECT_EQ(dst.bits(), bits);
  EXPECT_THAT(dst, ElementsAreArray(expected));
}

TYPED_TEST(
    packed_int_vec_core_test,
    compact_inline_source_to_heap_only_move_construction_uses_copy_fallback) {
  using compact_vec = std::conditional_t<
      TypeParam::type::has_inline_storage, typename TypeParam::type,
      compact_packed_int_vector<typename TypeParam::type::value_type>>;
  using heap_vec = packed_int_vector<typename TypeParam::type::value_type>;
  using value_type = typename compact_vec::value_type;
  using size_type = typename compact_vec::size_type;

  auto const bits =
      std::min<size_type>(size_type{3}, compact_vec::bits_per_block);
  auto const count = std::min<size_type>(
      compact_vec::inline_capacity_for_bits(bits), size_type{5});

  ASSERT_GT(count, 0u);

  compact_vec src(bits);
  std::vector<value_type> expected;
  expected.reserve(count);

  for (size_type i = 0; i < count; ++i) {
    auto const v = cross_type_test_value<value_type>(i);
    src.push_back(v);
    expected.push_back(v);
  }

  ASSERT_TRUE(src.is_inline());
  ASSERT_FALSE(src.uses_heap());

  heap_vec dst(std::move(src));

  EXPECT_FALSE(dst.is_inline());
  EXPECT_TRUE(dst.uses_heap());
  EXPECT_EQ(dst.bits(), bits);
  EXPECT_THAT(dst, ElementsAreArray(expected));

  expect_moved_from_empty(src);
}
