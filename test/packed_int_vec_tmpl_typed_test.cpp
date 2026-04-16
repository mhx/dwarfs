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

#include <dwarfs/container/packed_value_traits_optional.h>

#include "packed_int_vector_test_helpers.h"

using namespace dwarfs::test;
using namespace dwarfs::container;
using ::testing::Each;
using ::testing::ElementsAre;
using ::testing::ElementsAreArray;
using ::testing::Ge;

namespace {

using stress_value_type = uint32_t;

struct packable_struct {
  friend bool
  operator==(packable_struct const&, packable_struct const&) = default;
  friend std::strong_ordering
  operator<=>(packable_struct const&, packable_struct const&) = default;
  friend std::ostream& operator<<(std::ostream& os, packable_struct const& s) {
    return os << "{" << static_cast<int>(s.a) << ", " << static_cast<int>(s.b)
              << ", " << s.c << "}";
  }

  uint8_t a{};
  uint8_t b{};
  uint16_t c{};
};

constexpr size_t stress_digits = std::numeric_limits<stress_value_type>::digits;

constexpr std::array seeds{
    0x123456789abcdef0ULL, 0xfedcba9876543210ULL, 0xdeadbeefdeadbeefULL,
    0x0badc0de0badc0deULL, 0xabcdef0123456789ULL, 0x0123456789abcdefULL,
};

template <typename T>
T mask_for_bits(size_t bits) {
  using utype = std::make_unsigned_t<T>;
  constexpr size_t digits = std::numeric_limits<utype>::digits;

  if (bits == 0) {
    return T{0};
  }
  if (bits >= digits) {
    return static_cast<T>(~utype{0});
  }
  return static_cast<T>((utype{1} << bits) - 1);
}

template <typename T>
T random_signed_value_for_bits(std::mt19937_64& rng, size_t bits) {
  static_assert(std::is_signed_v<T>);
  using limits = std::numeric_limits<T>;
  constexpr size_t digits = limits::digits + 1; // includes sign bit

  if (bits == 0) {
    return T{0};
  }

  if (bits >= digits) {
    std::uniform_int_distribution<T> dist(limits::min(), limits::max());
    return dist(rng);
  }

  // [-2^(bits-1), 2^(bits-1)-1]
  auto const min_value = -(INT64_C(1) << (bits - 1));
  auto const max_value = (INT64_C(1) << (bits - 1)) - 1;

  std::uniform_int_distribution<int64_t> dist(min_value, max_value);
  return static_cast<T>(dist(rng));
}

inline stress_value_type
truncate_unsigned_to_bits(stress_value_type value, size_t bits) {
  if (bits == 0) {
    return 0;
  }
  if (bits >= stress_digits) {
    return value;
  }
  return value & ((stress_value_type{1} << bits) - 1);
}

template <typename Vec>
size_t required_bits_of(std::vector<typename Vec::value_type> const& values) {
  size_t bits = 0;
  for (auto v : values) {
    bits = std::max(bits, Vec::required_bits(v));
  }
  return bits;
}

template <typename Vec>
void expect_matches_model(Vec const& vec,
                          std::vector<typename Vec::value_type> const& model,
                          size_t current_bits) {
  ASSERT_EQ(vec.size(), model.size());
  ASSERT_EQ(vec.bits(), current_bits);
  ASSERT_EQ(vec.required_bits(), required_bits_of<Vec>(model));
  ASSERT_EQ(vec.empty(), model.empty());
  ASSERT_THAT(vec, ::testing::ElementsAreArray(model));

  for (size_t i = 0; i < model.size(); ++i) {
    ASSERT_EQ(vec[i], model[i]) << "index=" << i;
  }

  if (!model.empty()) {
    ASSERT_EQ(vec.front(), model.front());
    ASSERT_EQ(vec.back(), model.back());
  }
}

struct packed_int_vector_selector {
  template <dwarfs::container::packed_vector_value T>
  using type = packed_int_vector<T>;

  template <dwarfs::container::packed_vector_value T>
  using auto_type = auto_packed_int_vector<T>;
};

struct compact_packed_int_vector_selector {
  template <dwarfs::container::packed_vector_value T>
  using type = compact_packed_int_vector<T>;

  template <dwarfs::container::packed_vector_value T>
  using auto_type = compact_auto_packed_int_vector<T>;
};

} // namespace

namespace dwarfs::container {

template <>
struct packed_value_traits<packable_struct> {
  using encoded_type = uint32_t;

  static encoded_type encode(packable_struct const& value) {
    return (static_cast<uint32_t>(value.b) << 24) |
           (static_cast<uint32_t>(value.c) << 8) | value.a;
  }

  static packable_struct decode(encoded_type encoded) {
    return {
        .a = static_cast<uint8_t>(encoded & 0xFF),
        .b = static_cast<uint8_t>((encoded >> 24) & 0xFF),
        .c = static_cast<uint16_t>((encoded >> 8) & 0xFFFF),
    };
  }
};

} // namespace dwarfs::container

template <typename Vec>
class packed_int_vec_tmpl_test : public ::testing::Test {};

using packed_vector_types =
    ::testing::Types<packed_int_vector_selector,
                     compact_packed_int_vector_selector>;

TYPED_TEST_SUITE(packed_int_vec_tmpl_test, packed_vector_types);

TYPED_TEST(packed_int_vec_tmpl_test, basic) {
  using vec_type = typename TypeParam::template type<uint32_t>;
  vec_type vec(5);

  std::cout << vec_type::max_size() << std::endl;
  vec_type::dump_layout(std::cout);

  vec.push_back(1);
  vec.push_back(31);
  vec.push_back(0);
  vec.push_back(5);
  vec.push_back(3);
  vec.push_back(25);

  EXPECT_EQ(vec.size(), 6);
  EXPECT_EQ(vec.size_in_bytes(), 4);

  EXPECT_EQ(vec[0], 1);
  EXPECT_EQ(vec[1], 31);
  EXPECT_EQ(vec[2], 0);
  EXPECT_EQ(vec[3], 5);
  EXPECT_EQ(vec[4], 3);
  EXPECT_EQ(vec[5], 25);

  vec[0] = 11;
  EXPECT_EQ(vec[0], 11);

  vec.at(5) = 0;
  EXPECT_EQ(vec[5], 0);

  vec.resize(10);
  EXPECT_EQ(vec[1], 31);

  EXPECT_THROW(vec.at(10), std::out_of_range);
  EXPECT_THROW(vec.at(10) = 17, std::out_of_range);

  auto const& cvec = vec;

  EXPECT_EQ(cvec[0], 11);
  EXPECT_EQ(cvec[5], 0);

  EXPECT_THROW(cvec.at(10), std::out_of_range);

  vec.resize(4);
  vec.shrink_to_fit();

  if constexpr (vec_type::has_inline_storage) {
    EXPECT_EQ(vec.capacity(), vec_type::inline_capacity_for_bits(5));
  } else {
    EXPECT_EQ(vec.capacity(), 12);
  }

  EXPECT_EQ(vec[0], 11);
  EXPECT_FALSE(vec.empty());

  vec.clear();

  EXPECT_EQ(vec.size(), 0);
  EXPECT_TRUE(vec.empty());
  vec.shrink_to_fit();
  if constexpr (vec_type::has_inline_storage) {
    EXPECT_EQ(vec.capacity(), vec_type::inline_capacity_for_bits(5));
  } else {
    EXPECT_EQ(vec.capacity(), 0);
  }
  EXPECT_EQ(vec.size_in_bytes(), 0);
}

TYPED_TEST(packed_int_vec_tmpl_test, signed_int) {
  using vec_type = typename TypeParam::template type<int64_t>;
  vec_type vec(13);

  std::cout << vec_type::max_size() << std::endl;
  vec_type::dump_layout(std::cout);
  static_assert(vec_type::max_size() >= 4095,
                "test assumes max_size() >= 4095");

  for (int64_t i = -2048; i < 2047; ++i) {
    vec.push_back(i);
  }

  EXPECT_EQ(vec.size(), 4095);
  EXPECT_EQ(vec.size_in_bytes(), 6656);

  EXPECT_EQ(vec.front(), -2048);
  EXPECT_EQ(vec.back(), 2046);

  vec.resize(2048);

  for (int64_t i = 0; i < 2048; ++i) {
    EXPECT_EQ(vec[i], i - 2048);
  }

  auto unpacked = vec.unpack();

  for (int64_t i = 0; i < 2048; ++i) {
    EXPECT_EQ(unpacked[i], i - 2048);
  }
}

TYPED_TEST(packed_int_vec_tmpl_test, zero_bits) {
  using vec_type = typename TypeParam::template type<uint32_t>;
  vec_type vec(0);

  for (uint32_t i = 0; i < 100; ++i) {
    vec.push_back(0);
  }

  EXPECT_EQ(vec.size(), 100);
  EXPECT_EQ(vec.size_in_bytes(), 0);

  for (uint32_t i = 0; i < 100; ++i) {
    EXPECT_EQ(vec[i], 0);
  }
}

TYPED_TEST(packed_int_vec_tmpl_test,
           resize_grow_zero_initializes_new_elements) {
  using vec_type = typename TypeParam::template type<uint32_t>;
  vec_type vec(5);

  for (uint32_t v : {1, 31, 7, 9, 3, 25}) {
    vec.push_back(v);
  }

  vec.resize(4);
  vec.resize(6);

  EXPECT_THAT(vec, ElementsAre(1, 31, 7, 9, 0, 0));
}

TYPED_TEST(packed_int_vec_tmpl_test, cross_block_round_trip) {
  using vec_type = typename TypeParam::template type<uint32_t>;
  vec_type vec(17);

  std::vector<uint32_t> values{0, 1, 17, 12345, 65535, 131071, 42, 99999};

  for (auto v : values) {
    vec.push_back(v);
  }

  EXPECT_THAT(vec, ElementsAreArray(values));
}

TYPED_TEST(packed_int_vec_tmpl_test, full_width_unsigned_round_trip) {
  using vec_type = typename TypeParam::template type<uint32_t>;
  vec_type vec(32);

  std::vector<uint32_t> values{0, 1, 0x7fffffff, 0x80000000, 0xffffffff};

  for (auto v : values) {
    vec.push_back(v);
  }

  EXPECT_THAT(vec, ElementsAreArray(values));
}

TYPED_TEST(packed_int_vec_tmpl_test, full_width_signed_round_trip) {
  using vec_type = typename TypeParam::template type<int64_t>;
  vec_type vec(64);

  std::vector<int64_t> values{std::numeric_limits<int64_t>::min(), -1, 0, 1,
                              std::numeric_limits<int64_t>::max()};

  for (auto v : values) {
    vec.push_back(v);
  }

  EXPECT_THAT(vec, ElementsAreArray(values));
}

TYPED_TEST(packed_int_vec_tmpl_test, copy_is_independent) {
  using vec_type = typename TypeParam::template type<uint32_t>;
  vec_type vec(6);

  for (uint32_t v : {1, 2, 3, 4}) {
    vec.push_back(v);
  }

  auto copy = vec;
  copy[1] = 55;
  copy.push_back(12);

  EXPECT_THAT(vec, ElementsAre(1, 2, 3, 4));
  EXPECT_THAT(copy, ElementsAre(1, 55, 3, 4, 12));
}

TYPED_TEST(packed_int_vec_tmpl_test, reset_reinitializes_storage_and_metadata) {
  using vec_type = typename TypeParam::template type<uint32_t>;
  vec_type vec(5);

  for (uint32_t v : {1, 2, 3, 4}) {
    vec.push_back(v);
  }

  vec.reset(9, 3);

  EXPECT_EQ(vec.bits(), 9);
  EXPECT_EQ(vec.size(), 3);
  EXPECT_EQ(vec.size_in_bytes(), 4);
  EXPECT_THAT(vec, ElementsAre(0, 0, 0));
}

TYPED_TEST(packed_int_vec_tmpl_test, reserve_preserves_contents) {
  using vec_type = typename TypeParam::template type<uint32_t>;
  vec_type vec(5);

  vec.push_back(7);
  vec.push_back(11);

  vec.reserve(100);

  EXPECT_THAT(vec, ElementsAre(7, 11));
  EXPECT_THAT(vec.capacity(), Ge(100));
}

TYPED_TEST(packed_int_vec_tmpl_test, zero_bits_always_reads_as_zero) {
  using vec_type = typename TypeParam::template type<uint32_t>;
  vec_type vec(0);

  vec.push_back(123);
  vec.push_back(456);
  vec.resize(4);
  vec[1] = 999;

  EXPECT_EQ(vec.size(), 4);
  EXPECT_EQ(vec.size_in_bytes(), 0);
  EXPECT_THAT(vec, Each(0));
}

TYPED_TEST(packed_int_vec_tmpl_test,
           fixed_width_assignment_truncates_without_touching_neighbours) {
  using vec_type = typename TypeParam::template type<uint32_t>;
  vec_type vec(5);

  vec.push_back(1);
  vec.push_back(2);
  vec.push_back(3);

  vec[1] = 63; // 0b111111 -> truncated to 0b11111 == 31

  EXPECT_EQ(vec.bits(), 5);
  EXPECT_THAT(vec, ElementsAre(1, 31, 3));
}

TYPED_TEST(
    packed_int_vec_tmpl_test,
    fixed_width_assignment_truncates_without_touching_cross_block_neighbours) {
  using vec_type = typename TypeParam::template type<uint32_t>;
  vec_type vec(5);

  // 7 elements at 5 bits each span more than one 32-bit block.
  for (uint32_t v : {1, 2, 3, 4, 5, 6, 7}) {
    vec.push_back(v);
  }

  vec[5] = 255; // truncated to 31

  EXPECT_EQ(vec.bits(), 5);
  EXPECT_THAT(vec, ElementsAre(1, 2, 3, 4, 5, 31, 7));
}

TYPED_TEST(
    packed_int_vec_tmpl_test,
    fixed_width_signed_assignment_truncates_without_touching_neighbours) {
  using vec_type = typename TypeParam::template type<int32_t>;
  vec_type vec(3);

  vec.push_back(-1);
  vec.push_back(0);
  vec.push_back(1);

  vec[1] = 7; // fits in 3 signed bits as -1 after truncation/reinterpretation

  EXPECT_EQ(vec.bits(), 3);
  EXPECT_THAT(vec, ElementsAre(-1, -1, 1));
}

TYPED_TEST(packed_int_vec_tmpl_test, required_bits_unsigned_static) {
  using vec_type = typename TypeParam::template type<uint32_t>;

  EXPECT_EQ(vec_type::required_bits(0), 0);
  EXPECT_EQ(vec_type::required_bits(1), 1);
  EXPECT_EQ(vec_type::required_bits(2), 2);
  EXPECT_EQ(vec_type::required_bits(3), 2);
  EXPECT_EQ(vec_type::required_bits(4), 3);
  EXPECT_EQ(vec_type::required_bits(31), 5);
  EXPECT_EQ(vec_type::required_bits(32), 6);
  EXPECT_EQ(vec_type::required_bits(std::numeric_limits<uint32_t>::max()), 32);
}

TYPED_TEST(packed_int_vec_tmpl_test, required_bits_signed_static) {
  using vec_type = typename TypeParam::template type<int32_t>;

  EXPECT_EQ(vec_type::required_bits(0), 0);
  EXPECT_EQ(vec_type::required_bits(-1), 1);
  EXPECT_EQ(vec_type::required_bits(1), 2);
  EXPECT_EQ(vec_type::required_bits(-2), 2);
  EXPECT_EQ(vec_type::required_bits(2), 3);
  EXPECT_EQ(vec_type::required_bits(3), 3);
  EXPECT_EQ(vec_type::required_bits(-3), 3);
  EXPECT_EQ(vec_type::required_bits(-4), 3);
  EXPECT_EQ(vec_type::required_bits(4), 4);
  EXPECT_EQ(vec_type::required_bits(std::numeric_limits<int32_t>::min()), 32);
  EXPECT_EQ(vec_type::required_bits(std::numeric_limits<int32_t>::max()), 32);
}

TYPED_TEST(packed_int_vec_tmpl_test, required_bits_member) {
  using vec_type = typename TypeParam::template auto_type<int32_t>;
  vec_type vec(0, 4);

  vec[0] = -1;
  vec[1] = 3;
  vec[2] = -8;
  vec[3] = 0;

  EXPECT_EQ(vec.required_bits(), 4);
  EXPECT_EQ(vec.bits(), 4);
  EXPECT_THAT(vec, ElementsAre(-1, 3, -8, 0));
}

TYPED_TEST(packed_int_vec_tmpl_test, auto_push_back_grows_bit_width) {
  using vec_type = typename TypeParam::template auto_type<uint32_t>;
  vec_type vec(0);

  vec.push_back(0);
  EXPECT_EQ(vec.bits(), 0);
  EXPECT_THAT(vec, ElementsAre(0));

  vec.push_back(1);
  EXPECT_EQ(vec.bits(), 1);
  EXPECT_THAT(vec, ElementsAre(0, 1));

  vec.push_back(7);
  EXPECT_EQ(vec.bits(), 3);
  EXPECT_THAT(vec, ElementsAre(0, 1, 7));

  vec.push_back(31);
  EXPECT_EQ(vec.bits(), 5);
  EXPECT_THAT(vec, ElementsAre(0, 1, 7, 31));
}

TYPED_TEST(packed_int_vec_tmpl_test, auto_set_grows_bit_width) {
  using vec_type = typename TypeParam::template auto_type<uint32_t>;
  vec_type vec(0, 3);

  EXPECT_EQ(vec.bits(), 0);
  EXPECT_THAT(vec, ElementsAre(0, 0, 0));

  vec[1] = 9;
  EXPECT_EQ(vec.bits(), 4);
  EXPECT_THAT(vec, ElementsAre(0, 9, 0));

  vec[2] = 31;
  EXPECT_EQ(vec.bits(), 5);
  EXPECT_THAT(vec, ElementsAre(0, 9, 31));
}

TYPED_TEST(packed_int_vec_tmpl_test,
           auto_resize_grows_once_and_fills_new_values) {
  using vec_type = typename TypeParam::template auto_type<uint32_t>;
  vec_type vec(2);

  vec.push_back(1);
  vec.push_back(2);

  vec.resize(5, 17);

  EXPECT_EQ(vec.bits(), 5);
  EXPECT_EQ(vec.size(), 5);
  EXPECT_THAT(vec, ElementsAre(1, 2, 17, 17, 17));
}

TYPED_TEST(packed_int_vec_tmpl_test,
           auto_resize_shrink_does_not_change_bit_width) {
  using vec_type = typename TypeParam::template auto_type<uint32_t>;
  vec_type vec(2);

  vec.push_back(1);
  vec.push_back(2);
  vec.resize(5, 17);

  ASSERT_EQ(vec.bits(), 5);

  vec.resize(2);

  EXPECT_EQ(vec.bits(), 5);
  EXPECT_THAT(vec, ElementsAre(1, 2));
}

TYPED_TEST(packed_int_vec_tmpl_test, optimize_storage_shrinks_bit_width) {
  using vec_type = typename TypeParam::template auto_type<uint32_t>;
  vec_type vec(7);

  vec.push_back(3);
  vec.push_back(4);

  ASSERT_EQ(vec.bits(), 7);

  vec.optimize_storage();

  EXPECT_EQ(vec.bits(), 3);
  EXPECT_EQ(vec.required_bits(), 3);
  EXPECT_THAT(vec, ElementsAre(3, 4));
}

TYPED_TEST(packed_int_vec_tmpl_test, optimize_storage_can_reduce_to_zero_bits) {
  using vec_type = typename TypeParam::template auto_type<uint32_t>;
  vec_type vec(5);

  vec.push_back(17);
  vec.push_back(3);

  vec[0] = 0;
  vec[1] = 0;

  ASSERT_EQ(vec.required_bits(), 0);

  vec.optimize_storage();

  EXPECT_EQ(vec.bits(), 0);
  EXPECT_EQ(vec.size(), 2);
  EXPECT_EQ(vec.size_in_bytes(), 0);
  EXPECT_THAT(vec, ElementsAre(0, 0));
}

TYPED_TEST(packed_int_vec_tmpl_test, truncate_to_bits_can_widen_and_narrow) {
  using vec_type = typename TypeParam::template auto_type<uint32_t>;
  vec_type vec(5);

  vec.push_back(1);
  vec.push_back(31);
  vec.push_back(7);

  vec.truncate_to_bits(8);
  EXPECT_EQ(vec.bits(), 8);
  EXPECT_THAT(vec, ElementsAre(1, 31, 7));

  vec.truncate_to_bits(3);
  EXPECT_EQ(vec.bits(), 3);
  EXPECT_THAT(vec, ElementsAre(1, 7, 7));
}

TYPED_TEST(packed_int_vec_tmpl_test,
           truncate_to_bits_zero_clears_storage_for_zero_values) {
  using vec_type = typename TypeParam::template auto_type<uint32_t>;
  vec_type vec(5, 4);

  EXPECT_EQ(vec.bits(), 5);
  EXPECT_EQ(vec.size(), 4);
  EXPECT_EQ(vec.size_in_bytes(), 4);

  vec.truncate_to_bits(0);

  EXPECT_EQ(vec.bits(), 0);
  EXPECT_EQ(vec.size(), 4);
  EXPECT_EQ(vec.size_in_bytes(), 0);
  EXPECT_THAT(vec, ElementsAre(0, 0, 0, 0));
}

TYPED_TEST(packed_int_vec_tmpl_test, invalid_bit_width_throws) {
  using vec_type = typename TypeParam::template type<uint32_t>;

  EXPECT_THROW((vec_type(33)), std::invalid_argument);
  EXPECT_THROW((vec_type(33, 1)), std::invalid_argument);

  vec_type vec(5);

  EXPECT_THROW(vec.reset(33, 0), std::invalid_argument);
  EXPECT_THROW(vec.truncate_to_bits(33), std::invalid_argument);
}

TYPED_TEST(packed_int_vec_tmpl_test,
           policy_capacity_limit_throws_on_full_width_reserve) {
  using vec_type = typename TypeParam::template type<uint32_t>;
  using size_type = typename vec_type::size_type;

  using layout_type =
      detail::packed_vector_layout<typename vec_type::policy_type,
                                   typename vec_type::value_type,
                                   typename vec_type::underlying_type>;

  if constexpr (layout_type::max_capacity_blocks_value <
                std::numeric_limits<size_type>::max()) {
    vec_type vec(std::numeric_limits<uint32_t>::digits);

    EXPECT_THROW(vec.reserve(layout_type::max_capacity_blocks_value + 1),
                 std::length_error);
  }
}

TYPED_TEST(packed_int_vec_tmpl_test, auto_clear_preserves_bit_width) {
  using vec_type = typename TypeParam::template auto_type<uint32_t>;
  vec_type vec(5);

  vec.push_back(17);
  vec.push_back(3);

  vec.clear();

  EXPECT_EQ(vec.bits(), 5);
  EXPECT_EQ(vec.size(), 0);
  EXPECT_EQ(vec.size_in_bytes(), 0);
  EXPECT_TRUE(vec.empty());

  vec.push_back(7);
  EXPECT_EQ(vec.bits(), 5);
  EXPECT_THAT(vec, ElementsAre(7));
}

TYPED_TEST(packed_int_vec_tmpl_test, copy_constructor_and_assignment) {
  using vec_type = typename TypeParam::template type<uint32_t>;
  vec_type vec(5);

  vec.push_back(1);
  vec.push_back(31);
  vec.push_back(7);

  vec_type copy{vec};

  EXPECT_EQ(copy.bits(), 5);
  EXPECT_THAT(copy, ElementsAre(1, 31, 7));

  copy[1] = 3;

  EXPECT_THAT(vec, ElementsAre(1, 31, 7));
  EXPECT_THAT(copy, ElementsAre(1, 3, 7));

  vec_type assigned(2);
  assigned = vec;

  EXPECT_EQ(assigned.bits(), 5);
  EXPECT_THAT(assigned, ElementsAre(1, 31, 7));

  assigned[0] = 0;

  EXPECT_THAT(vec, ElementsAre(1, 31, 7));
  EXPECT_THAT(assigned, ElementsAre(0, 31, 7));
}

TYPED_TEST(packed_int_vec_tmpl_test, move_constructor_and_assignment) {
  using vec_type = typename TypeParam::template type<uint32_t>;
  vec_type vec(5);

  vec.push_back(1);
  vec.push_back(31);
  vec.push_back(7);

  vec_type moved{std::move(vec)};

  EXPECT_EQ(moved.bits(), 5);
  EXPECT_THAT(moved, ElementsAre(1, 31, 7));

  vec_type other(2);
  other.push_back(1);
  other.push_back(2);

  other = std::move(moved);

  EXPECT_EQ(other.bits(), 5);
  EXPECT_THAT(other, ElementsAre(1, 31, 7));
}

TYPED_TEST(packed_int_vec_tmpl_test, auto_copy_and_move_preserve_bit_width) {
  using vec_type = typename TypeParam::template auto_type<uint32_t>;
  vec_type vec(0);

  vec.push_back(1);
  vec.push_back(31);
  vec.push_back(7);

  EXPECT_EQ(vec.bits(), 5);

  vec_type copy{vec};
  EXPECT_EQ(copy.bits(), 5);
  EXPECT_THAT(copy, ElementsAre(1, 31, 7));

  vec_type moved{std::move(copy)};
  EXPECT_EQ(moved.bits(), 5);
  EXPECT_THAT(moved, ElementsAre(1, 31, 7));
}

TYPED_TEST(packed_int_vec_tmpl_test,
           auto_repack_from_zero_bits_preserves_existing_zeros) {
  using vec_type = typename TypeParam::template auto_type<uint32_t>;
  vec_type vec(0, 4);

  EXPECT_EQ(vec.bits(), 0);
  EXPECT_EQ(vec.size_in_bytes(), 0);

  vec[2] = 7;

  EXPECT_EQ(vec.bits(), 3);
  EXPECT_THAT(vec, ElementsAre(0, 0, 7, 0));
}

TYPED_TEST(packed_int_vec_tmpl_test, can_store_regular_enums) {
  enum test_enum : std::uint16_t {
    value1 = 0,
    value2 = 1,
    value3 = 2,
    value4 = 63,
  };

  using vec_type = typename TypeParam::template auto_type<test_enum>;

  EXPECT_EQ(vec_type::required_bits(value1), 0);
  EXPECT_EQ(vec_type::required_bits(value4), 6);

  vec_type vec;

  vec.push_back(value1);

  EXPECT_EQ(vec.bits(), 0);

  vec.push_back(value2);
  vec.push_back(value3);

  EXPECT_EQ(vec.bits(), 2);

  EXPECT_THAT(vec, ElementsAre(value1, value2, value3));

  std::vector<test_enum> expected{value1, value2, value3};
  EXPECT_EQ(vec.unpack(), expected);
}

TYPED_TEST(packed_int_vec_tmpl_test, can_store_class_enums) {
  enum class test_enum {
    value1 = 0,
    value2 = 1,
    value3 = 2,
    value4 = 63,
  };

  using vec_type = typename TypeParam::template auto_type<test_enum>;

  EXPECT_EQ(vec_type::required_bits(test_enum::value1), 0);
  EXPECT_EQ(vec_type::required_bits(test_enum::value4), 7);

  vec_type vec;

  vec.push_back(test_enum::value1);

  EXPECT_EQ(vec.bits(), 0);

  vec.push_back(test_enum::value2);
  vec.push_back(test_enum::value3);

  EXPECT_EQ(vec.bits(), 3);

  EXPECT_THAT(vec, ElementsAre(test_enum::value1, test_enum::value2,
                               test_enum::value3));

  std::vector<test_enum> expected{test_enum::value1, test_enum::value2,
                                  test_enum::value3};
  EXPECT_EQ(vec.unpack(), expected);
}

TYPED_TEST(packed_int_vec_tmpl_test, can_store_type_with_custom_traits) {
  using vec_type =
      typename TypeParam::template auto_type<std::optional<uint32_t>>;
  vec_type vec;

  EXPECT_EQ(vec_type::required_bits(std::nullopt), 0);
  EXPECT_EQ(vec_type::required_bits(0), 1);

  vec.push_back(std::nullopt);

  EXPECT_EQ(vec.bits(), 0);

  vec.push_back(42);
  vec.push_back(12345);

  EXPECT_THAT(vec, ElementsAre(std::nullopt, std::optional<uint32_t>{42},
                               std::optional<uint32_t>{12345}));

  std::vector<std::optional<uint32_t>> expected{std::nullopt,
                                                std::optional<uint32_t>{42},
                                                std::optional<uint32_t>{12345}};
  EXPECT_EQ(vec.unpack(), expected);
}

TYPED_TEST(packed_int_vec_tmpl_test, custom_types_are_ordered_correctly) {
  using vec_type = typename TypeParam::template auto_type<packable_struct>;

  std::mt19937_64 rng(4711);
  std::uniform_int_distribution<unsigned> dist8(0, 255);
  std::uniform_int_distribution<unsigned> dist16(0, 65535);

  static constexpr size_t num_values = 100;
  std::vector<packable_struct> input;
  for (size_t i = 0; i < num_values; ++i) {
    input.push_back({.a = static_cast<uint8_t>(dist8(rng)),
                     .b = static_cast<uint8_t>(dist8(rng)),
                     .c = static_cast<uint16_t>(dist16(rng))});
  }

  EXPECT_FALSE(std::ranges::is_sorted(input));

  std::vector<packable_struct> sorted_input = input;
  std::ranges::sort(sorted_input);

  vec_type vec;

  EXPECT_EQ(vec.bits(), 0);

  for (auto const& value : input) {
    vec.push_back(value);
  }

  EXPECT_THAT(vec, ElementsAreArray(input));

  std::ranges::sort(vec);

  EXPECT_THAT(vec, ElementsAreArray(sorted_input));
}

TYPED_TEST(packed_int_vec_tmpl_test, custom_types_operations) {
  using vec_type = typename TypeParam::template auto_type<packable_struct>;

  vec_type vec{
      packable_struct{.a = 5, .b = 4, .c = 3},
      packable_struct{.a = 2, .b = 1, .c = 0},
  };

  vec.resize(3, {.a = 1, .b = 2, .c = 3});

  EXPECT_EQ(vec.size(), 3);
  EXPECT_THAT(vec, ElementsAre(packable_struct{.a = 5, .b = 4, .c = 3},
                               packable_struct{.a = 2, .b = 1, .c = 0},
                               packable_struct{.a = 1, .b = 2, .c = 3}));

  vec.insert(vec.begin() + 1, {.a = 4, .b = 5, .c = 6});

  vec.insert(vec.begin() + 3, {
                                  packable_struct{.a = 9, .b = 8, .c = 7},
                                  packable_struct{.a = 6, .b = 5, .c = 4},
                              });

  EXPECT_EQ(vec.size(), 6);
  EXPECT_THAT(vec, ElementsAre(packable_struct{.a = 5, .b = 4, .c = 3},
                               packable_struct{.a = 4, .b = 5, .c = 6},
                               packable_struct{.a = 2, .b = 1, .c = 0},
                               packable_struct{.a = 9, .b = 8, .c = 7},
                               packable_struct{.a = 6, .b = 5, .c = 4},
                               packable_struct{.a = 1, .b = 2, .c = 3}));
}

TYPED_TEST(packed_int_vec_tmpl_test,
           fixed_width_enum_values_truncate_via_encoded_representation) {
  enum class test_enum : uint8_t {
    a = 0,
    b = 1,
    c = 3,
    d = 7,
  };

  using vec_type = typename TypeParam::template type<test_enum>;

  vec_type vec(2);
  vec.push_back(test_enum::d); // 7 -> truncated to 3
  vec.push_back(test_enum::b);

  EXPECT_EQ(vec.bits(), 2);
  EXPECT_THAT(vec, ElementsAre(test_enum::c, test_enum::b));
}

TYPED_TEST(packed_int_vec_tmpl_test,
           signed_enum_round_trips_and_uses_signed_required_bits) {
  enum class signed_enum : int8_t {
    minus_one = -1,
    minus_two = -2,
    zero = 0,
    plus_one = 1,
  };

  using vec_type = typename TypeParam::template auto_type<signed_enum>;

  EXPECT_EQ(vec_type::required_bits(signed_enum::zero), 0);
  EXPECT_EQ(vec_type::required_bits(signed_enum::minus_one), 1);
  EXPECT_EQ(vec_type::required_bits(signed_enum::minus_two), 2);
  EXPECT_EQ(vec_type::required_bits(signed_enum::plus_one), 2);

  vec_type vec;
  vec.push_back(signed_enum::minus_one);
  vec.push_back(signed_enum::plus_one);
  vec.push_back(signed_enum::minus_two);

  EXPECT_EQ(vec.bits(), 2);
  EXPECT_THAT(vec, ElementsAre(signed_enum::minus_one, signed_enum::plus_one,
                               signed_enum::minus_two));
}

TYPED_TEST(packed_int_vec_tmpl_test,
           fixed_width_custom_trait_type_truncates_encoded_values) {
  using vec_type = typename TypeParam::template type<std::optional<uint32_t>>;

  vec_type vec(3);
  vec.push_back(std::nullopt); // encode = 0
  vec.push_back(0);            // encode = 1
  vec.push_back(7);            // encode = 8 -> truncated to 0

  EXPECT_EQ(vec.bits(), 3);
  EXPECT_THAT(
      vec, ElementsAre(std::nullopt, std::optional<uint32_t>{0}, std::nullopt));
}

TYPED_TEST(packed_int_vec_tmpl_test,
           custom_type_copy_and_move_preserve_values) {
  using vec_type = typename TypeParam::template auto_type<packable_struct>;

  vec_type vec{
      {.a = 1, .b = 2, .c = 3},
      {.a = 4, .b = 5, .c = 6},
      {.a = 7, .b = 8, .c = 9},
  };

  vec_type copy{vec};
  EXPECT_THAT(copy, ElementsAre(packable_struct{.a = 1, .b = 2, .c = 3},
                                packable_struct{.a = 4, .b = 5, .c = 6},
                                packable_struct{.a = 7, .b = 8, .c = 9}));

  vec_type moved{std::move(copy)};
  EXPECT_THAT(moved, ElementsAre(packable_struct{.a = 1, .b = 2, .c = 3},
                                 packable_struct{.a = 4, .b = 5, .c = 6},
                                 packable_struct{.a = 7, .b = 8, .c = 9}));
}

TYPED_TEST(packed_int_vec_tmpl_test, custom_type_range_apis_work) {
  using vec_type = typename TypeParam::template auto_type<packable_struct>;

  std::array src{
      packable_struct{.a = 1, .b = 2, .c = 3},
      packable_struct{.a = 4, .b = 5, .c = 6},
  };

  vec_type vec(dwarfs::from_range, src);
  EXPECT_THAT(vec, ElementsAre(src[0], src[1]));

  vec.append_range(std::array{
      packable_struct{.a = 7, .b = 8, .c = 9},
  });
  EXPECT_THAT(vec, ElementsAre(src[0], src[1],
                               packable_struct{.a = 7, .b = 8, .c = 9}));

  vec.insert_range(vec.begin() + 1, std::array{
                                        packable_struct{.a = 9, .b = 9, .c = 9},
                                    });
  EXPECT_THAT(vec, ElementsAre(packable_struct{.a = 1, .b = 2, .c = 3},
                               packable_struct{.a = 9, .b = 9, .c = 9},
                               packable_struct{.a = 4, .b = 5, .c = 6},
                               packable_struct{.a = 7, .b = 8, .c = 9}));

  vec.assign_range(std::array{
      packable_struct{.a = 3, .b = 2, .c = 1},
  });
  EXPECT_THAT(vec, ElementsAre(packable_struct{.a = 3, .b = 2, .c = 1}));
}

TYPED_TEST(packed_int_vec_tmpl_test,
           required_bits_member_works_for_custom_types) {
  using vec_type =
      typename TypeParam::template auto_type<std::optional<uint32_t>>;

  vec_type vec;
  vec.push_back(std::nullopt);
  vec.push_back(0);
  vec.push_back(12345);

  EXPECT_EQ(vec.required_bits(), vec_type::required_bits(12345));
}

TYPED_TEST(packed_int_vec_tmpl_test, value_proxy_plus_equals_and_minus_equals) {
  using vec_type = typename TypeParam::template auto_type<uint32_t>;

  vec_type vec{10, 20};

  vec[0] += 5;
  vec[1] -= 7;

  EXPECT_THAT(vec.unpack(), ElementsAre(15, 13));
}

TYPED_TEST(packed_int_vec_tmpl_test,
           value_proxy_prefix_increment_and_decrement) {
  using vec_type = typename TypeParam::template auto_type<uint32_t>;

  vec_type vec{10};

  // Careful: using `auto` here would create a copy of the proxy, *not* the
  // value.
  uint32_t const inc_result = ++vec[0];
  uint32_t const dec_result = --vec[0];

  EXPECT_EQ(inc_result, 11);
  EXPECT_EQ(dec_result, 10);
  EXPECT_THAT(vec.unpack(), ElementsAre(10));
}

TYPED_TEST(packed_int_vec_tmpl_test,
           value_proxy_postfix_increment_and_decrement) {
  using vec_type = typename TypeParam::template auto_type<uint32_t>;

  vec_type vec{10};

  auto inc_result = vec[0]++;
  auto dec_result = vec[0]--;

  EXPECT_EQ(inc_result, 10);
  EXPECT_EQ(dec_result, 11);
  EXPECT_THAT(vec.unpack(), ElementsAre(10));
}

TYPED_TEST(packed_int_vec_tmpl_test, value_proxy_load_scalar) {
  using vec_type = typename TypeParam::template auto_type<uint32_t>;

  vec_type vec{10, 20};

  auto p0 = vec[0];
  auto p1 = vec[1];

  EXPECT_EQ(p0.load(), 10);
  EXPECT_EQ(p1.load(), 20);
}

TYPED_TEST(packed_int_vec_tmpl_test, value_proxy_unary_plus_scalar) {
  using vec_type = typename TypeParam::template auto_type<uint32_t>;

  vec_type vec{10, 20};

  auto p0 = vec[0];
  auto p1 = vec[1];

  EXPECT_EQ(+p0, 10);
  EXPECT_EQ(+p1, 20);
}

#ifdef __clang__
#pragma clang optimize on
#endif

TYPED_TEST(packed_int_vec_tmpl_test, stress_round_trip_unsigned) {
  using value_type = uint32_t;
  using vec_type = typename TypeParam::template type<value_type>;
  constexpr size_t digits = std::numeric_limits<value_type>::digits;

  std::mt19937_64 rng(4711);

  for (size_t bits = 0; bits <= digits; ++bits) {
    SCOPED_TRACE(::testing::Message() << "bits=" << bits);

    vec_type vec(bits);
    std::vector<value_type> expected;
    expected.reserve(2000);

    auto const mask = mask_for_bits<value_type>(bits);
    std::uniform_int_distribution<value_type> dist(
        0, std::numeric_limits<value_type>::max());

    for (size_t i = 0; i < 2000; ++i) {
      value_type value = bits == 0 ? 0 : (dist(rng) & mask);
      vec.push_back(value);
      expected.push_back(value);
    }

    EXPECT_EQ(vec.size(), expected.size());
    EXPECT_THAT(vec, ElementsAreArray(expected));

    for (size_t i = 0; i < expected.size(); ++i) {
      EXPECT_EQ(vec[i], expected[i]) << "bits=" << bits << ", i=" << i;
    }

    if (!expected.empty()) {
      EXPECT_EQ(vec.front(), expected.front());
      EXPECT_EQ(vec.back(), expected.back());
    }
  }
}

TYPED_TEST(packed_int_vec_tmpl_test, stress_round_trip_signed) {
  using value_type = int32_t;
  using vec_type = typename TypeParam::template type<value_type>;
  constexpr size_t digits =
      std::numeric_limits<std::make_unsigned_t<value_type>>::digits;

  std::mt19937_64 rng(4711);

  for (size_t bits = 0; bits <= digits; ++bits) {
    SCOPED_TRACE(::testing::Message() << "bits=" << bits);

    vec_type vec(bits);
    std::vector<value_type> expected;
    expected.reserve(2000);

    for (size_t i = 0; i < 2000; ++i) {
      auto value = random_signed_value_for_bits<value_type>(rng, bits);
      vec.push_back(value);
      expected.push_back(value);
    }

    EXPECT_EQ(vec.size(), expected.size());
    EXPECT_THAT(vec, ElementsAreArray(expected));

    for (size_t i = 0; i < expected.size(); ++i) {
      EXPECT_EQ(vec[i], expected[i]) << "bits=" << bits << ", i=" << i;
    }

    if (!expected.empty()) {
      EXPECT_EQ(vec.front(), expected.front());
      EXPECT_EQ(vec.back(), expected.back());
    }
  }
}

TYPED_TEST(packed_int_vec_tmpl_test, stress_mixed_operations_unsigned) {
  using value_type = uint32_t;
  using vec_type = typename TypeParam::template type<value_type>;
  constexpr size_t digits = std::numeric_limits<value_type>::digits;

  std::mt19937_64 rng(4711);

  for (size_t bits = 0; bits <= digits; ++bits) {
    SCOPED_TRACE(::testing::Message() << "bits=" << bits);

    vec_type vec(bits);
    std::vector<value_type> expected;

    auto const mask = mask_for_bits<value_type>(bits);
    std::uniform_int_distribution<int> op_dist(0, 99);
    std::uniform_int_distribution<value_type> value_dist(
        0, std::numeric_limits<value_type>::max());

    for (size_t step = 0; step < 1000; ++step) {
      int const op = op_dist(rng);

      if (op < 45) {
        value_type value = bits == 0 ? 0 : (value_dist(rng) & mask);
        vec.push_back(value);
        expected.push_back(value);
      } else if (op < 65) {
        if (!expected.empty()) {
          vec.pop_back();
          expected.pop_back();
        }
      } else if (op < 80) {
        size_t new_size =
            expected.empty() ? 0 : (rng() % (expected.size() + 20));
        vec.resize(new_size);
        expected.resize(new_size, 0);
      } else {
        if (!expected.empty()) {
          size_t i = rng() % expected.size();
          value_type value = bits == 0 ? 0 : (value_dist(rng) & mask);
          vec[i] = value;
          expected[i] = value;
        }
      }

      ASSERT_EQ(vec.size(), expected.size()) << "step=" << step;

      for (size_t i = 0; i < expected.size(); ++i) {
        ASSERT_EQ(vec[i], expected[i])
            << "bits=" << bits << ", step=" << step << ", i=" << i;
      }

      ASSERT_THAT(vec.unpack(), ElementsAreArray(expected));

      if (!expected.empty()) {
        ASSERT_EQ(vec.front(), expected.front());
        ASSERT_EQ(vec.back(), expected.back());
      }
    }
  }
}

TYPED_TEST(packed_int_vec_tmpl_test, auto_mixed_operations_stress) {
  using stress_vec_type =
      typename TypeParam::template auto_type<stress_value_type>;

  for (auto seed : seeds) {
    SCOPED_TRACE(::testing::Message() << "seed=" << seed);

    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> op_dist(0, 99);
    std::uniform_int_distribution<stress_value_type> value_dist(
        0, std::numeric_limits<stress_value_type>::max());
    std::uniform_int_distribution<size_t> bits_dist(0, stress_digits);

    stress_vec_type vec(0);
    std::vector<stress_value_type> model;
    size_t current_bits = 0;

    for (size_t step = 0; step < 5000; ++step) {
      int const op = op_dist(rng);

      SCOPED_TRACE(::testing::Message()
                   << "step=" << step << ", op=" << op
                   << ", size=" << model.size() << ", bits=" << current_bits);

      if (op < 25) {
        // push_back
        auto value = value_dist(rng);
        vec.push_back(value);
        model.push_back(value);
        current_bits =
            std::max(current_bits, stress_vec_type::required_bits(value));

      } else if (op < 40) {
        // pop_back
        if (!model.empty()) {
          vec.pop_back();
          model.pop_back();
        }

      } else if (op < 60) {
        // indexed assignment
        if (!model.empty()) {
          size_t const i = rng() % model.size();
          auto const value = value_dist(rng);
          vec[i] = value;
          model[i] = value;
          current_bits =
              std::max(current_bits, stress_vec_type::required_bits(value));
        }

      } else if (op < 75) {
        // resize
        size_t const new_size = rng() % 128;
        auto const fill_value = value_dist(rng);

        vec.resize(new_size, fill_value);

        if (new_size > model.size()) {
          current_bits = std::max(current_bits,
                                  stress_vec_type::required_bits(fill_value));
          model.resize(new_size, fill_value);
        } else {
          model.resize(new_size);
        }

      } else if (op < 85) {
        // optimize_storage
        vec.optimize_storage();
        current_bits = required_bits_of<stress_vec_type>(model);

      } else if (op < 95) {
        // truncate_to_bits
        size_t const new_bits = bits_dist(rng);
        vec.truncate_to_bits(new_bits);

        for (auto& v : model) {
          v = truncate_unsigned_to_bits(v, new_bits);
        }
        current_bits = new_bits;

      } else {
        // clear
        vec.clear();
        model.clear();
        // clear() preserves bits_
      }

      expect_matches_model(vec, model, current_bits);
    }
  }
}
