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
#include <limits>
#include <numeric>
#include <random>
#include <stdexcept>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <dwarfs/internal/compact_packed_int_vector.h>
#include <dwarfs/internal/packed_int_vector.h>
#include <dwarfs/internal/segmented_packed_int_vector.h>

using namespace dwarfs::internal;
using ::testing::Each;
using ::testing::ElementsAre;
using ::testing::ElementsAreArray;
using ::testing::Ge;

namespace {

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

using stress_value_type = uint32_t;
using segmented_vec = segmented_packed_int_vector<uint32_t, 4>;

constexpr size_t stress_digits = std::numeric_limits<stress_value_type>::digits;

constexpr std::array seeds{
    0x123456789abcdef0ULL, 0xfedcba9876543210ULL, 0xdeadbeefdeadbeefULL,
    0x0badc0de0badc0deULL, 0xabcdef0123456789ULL, 0x0123456789abcdefULL,
};

stress_value_type
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
  ASSERT_THAT(vec.unpack(), ElementsAreArray(model));

  for (size_t i = 0; i < model.size(); ++i) {
    ASSERT_EQ(vec[i], model[i]) << "index=" << i;
  }

  if (!model.empty()) {
    ASSERT_EQ(vec.front(), model.front());
    ASSERT_EQ(vec.back(), model.back());
  }
}

template <typename Vec>
void expect_matches_model(Vec const& vec,
                          std::vector<typename Vec::value_type> const& model) {
  ASSERT_EQ(vec.size(), model.size());
  ASSERT_EQ(vec.empty(), model.empty());
  ASSERT_THAT(vec.unpack(), ElementsAreArray(model));

  for (std::size_t i = 0; i < model.size(); ++i) {
    ASSERT_EQ(vec[i], model[i]) << "index=" << i;
  }

  if (!model.empty()) {
    ASSERT_EQ(vec.front(), model.front());
    ASSERT_EQ(vec.back(), model.back());
  }
}

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

struct packed_int_vector_selector {
  template <std::integral T>
  using type = packed_int_vector<T>;

  template <std::integral T>
  using auto_type = auto_packed_int_vector<T>;
};

struct compact_packed_int_vector_selector {
  template <std::integral T>
  using type = compact_packed_int_vector<T>;

  template <std::integral T>
  using auto_type = compact_auto_packed_int_vector<T>;
};

template <std::integral T>
struct packed_int_vector_type_selector {
  using type = packed_int_vector<T>;
  using auto_type = auto_packed_int_vector<T>;
};

template <std::integral T>
struct compact_packed_int_vector_type_selector {
  using type = compact_packed_int_vector<T>;
  using auto_type = compact_auto_packed_int_vector<T>;
};

} // namespace

template <typename Vec>
class packed_int_vector_test : public ::testing::Test {};

using packed_vector_types =
    ::testing::Types<packed_int_vector_selector,
                     compact_packed_int_vector_selector>;

TYPED_TEST_SUITE(packed_int_vector_test, packed_vector_types);

template <typename Vec>
class packed_int_vector_type_test : public ::testing::Test {};

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

TYPED_TEST_SUITE(packed_int_vector_type_test, packed_vector_type_types);

TYPED_TEST(packed_int_vector_test, basic) {
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
    EXPECT_EQ(vec.capacity(), 6);
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

TYPED_TEST(packed_int_vector_test, signed_int) {
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

TYPED_TEST(packed_int_vector_test, zero_bits) {
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

TYPED_TEST(packed_int_vector_test, resize_grow_zero_initializes_new_elements) {
  using vec_type = typename TypeParam::template type<uint32_t>;
  vec_type vec(5);

  for (uint32_t v : {1, 31, 7, 9, 3, 25}) {
    vec.push_back(v);
  }

  vec.resize(4);
  vec.resize(6);

  EXPECT_THAT(vec.unpack(), ElementsAre(1, 31, 7, 9, 0, 0));
}

TYPED_TEST(packed_int_vector_test, cross_block_round_trip) {
  using vec_type = typename TypeParam::template type<uint32_t>;
  vec_type vec(17);

  std::vector<uint32_t> values{0, 1, 17, 12345, 65535, 131071, 42, 99999};

  for (auto v : values) {
    vec.push_back(v);
  }

  EXPECT_THAT(vec.unpack(), ElementsAreArray(values));
}

TYPED_TEST(packed_int_vector_test, full_width_unsigned_round_trip) {
  using vec_type = typename TypeParam::template type<uint32_t>;
  vec_type vec(32);

  std::vector<uint32_t> values{0, 1, 0x7fffffff, 0x80000000, 0xffffffff};

  for (auto v : values) {
    vec.push_back(v);
  }

  EXPECT_THAT(vec.unpack(), ElementsAreArray(values));
}

TYPED_TEST(packed_int_vector_test, full_width_signed_round_trip) {
  using vec_type = typename TypeParam::template type<int64_t>;
  vec_type vec(64);

  std::vector<int64_t> values{std::numeric_limits<int64_t>::min(), -1, 0, 1,
                              std::numeric_limits<int64_t>::max()};

  for (auto v : values) {
    vec.push_back(v);
  }

  EXPECT_THAT(vec.unpack(), ElementsAreArray(values));
}

TYPED_TEST(packed_int_vector_test, copy_is_independent) {
  using vec_type = typename TypeParam::template type<uint32_t>;
  vec_type vec(6);

  for (uint32_t v : {1, 2, 3, 4}) {
    vec.push_back(v);
  }

  auto copy = vec;
  copy[1] = 55;
  copy.push_back(12);

  EXPECT_THAT(vec.unpack(), ElementsAre(1, 2, 3, 4));
  EXPECT_THAT(copy.unpack(), ElementsAre(1, 55, 3, 4, 12));
}

TYPED_TEST(packed_int_vector_test, reset_reinitializes_storage_and_metadata) {
  using vec_type = typename TypeParam::template type<uint32_t>;
  vec_type vec(5);

  for (uint32_t v : {1, 2, 3, 4}) {
    vec.push_back(v);
  }

  vec.reset(9, 3);

  EXPECT_EQ(vec.bits(), 9);
  EXPECT_EQ(vec.size(), 3);
  EXPECT_EQ(vec.size_in_bytes(), 4);
  EXPECT_THAT(vec.unpack(), ElementsAre(0, 0, 0));
}

TYPED_TEST(packed_int_vector_test, reserve_preserves_contents) {
  using vec_type = typename TypeParam::template type<uint32_t>;
  vec_type vec(5);

  vec.push_back(7);
  vec.push_back(11);

  vec.reserve(100);

  EXPECT_THAT(vec.unpack(), ElementsAre(7, 11));
  EXPECT_THAT(vec.capacity(), Ge(100));
}

TYPED_TEST(packed_int_vector_test, zero_bits_always_reads_as_zero) {
  using vec_type = typename TypeParam::template type<uint32_t>;
  vec_type vec(0);

  vec.push_back(123);
  vec.push_back(456);
  vec.resize(4);
  vec[1] = 999;

  EXPECT_EQ(vec.size(), 4);
  EXPECT_EQ(vec.size_in_bytes(), 0);
  EXPECT_THAT(vec.unpack(), Each(0));
}

TYPED_TEST(packed_int_vector_test, stress_round_trip_unsigned) {
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
    EXPECT_THAT(vec.unpack(), ElementsAreArray(expected));

    for (size_t i = 0; i < expected.size(); ++i) {
      EXPECT_EQ(vec[i], expected[i]) << "bits=" << bits << ", i=" << i;
    }

    if (!expected.empty()) {
      EXPECT_EQ(vec.front(), expected.front());
      EXPECT_EQ(vec.back(), expected.back());
    }
  }
}

TYPED_TEST(packed_int_vector_test, stress_round_trip_signed) {
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
    EXPECT_THAT(vec.unpack(), ElementsAreArray(expected));

    for (size_t i = 0; i < expected.size(); ++i) {
      EXPECT_EQ(vec[i], expected[i]) << "bits=" << bits << ", i=" << i;
    }

    if (!expected.empty()) {
      EXPECT_EQ(vec.front(), expected.front());
      EXPECT_EQ(vec.back(), expected.back());
    }
  }
}

TYPED_TEST(packed_int_vector_test, stress_mixed_operations_unsigned) {
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

TYPED_TEST(packed_int_vector_test,
           fixed_width_assignment_truncates_without_touching_neighbours) {
  using vec_type = typename TypeParam::template type<uint32_t>;
  vec_type vec(5);

  vec.push_back(1);
  vec.push_back(2);
  vec.push_back(3);

  vec[1] = 63; // 0b111111 -> truncated to 0b11111 == 31

  EXPECT_EQ(vec.bits(), 5);
  EXPECT_THAT(vec.unpack(), ElementsAre(1, 31, 3));
}

TYPED_TEST(
    packed_int_vector_test,
    fixed_width_assignment_truncates_without_touching_cross_block_neighbours) {
  using vec_type = typename TypeParam::template type<uint32_t>;
  vec_type vec(5);

  // 7 elements at 5 bits each span more than one 32-bit block.
  for (uint32_t v : {1, 2, 3, 4, 5, 6, 7}) {
    vec.push_back(v);
  }

  vec[5] = 255; // truncated to 31

  EXPECT_EQ(vec.bits(), 5);
  EXPECT_THAT(vec.unpack(), ElementsAre(1, 2, 3, 4, 5, 31, 7));
}

TYPED_TEST(
    packed_int_vector_test,
    fixed_width_signed_assignment_truncates_without_touching_neighbours) {
  using vec_type = typename TypeParam::template type<int32_t>;
  vec_type vec(3);

  vec.push_back(-1);
  vec.push_back(0);
  vec.push_back(1);

  vec[1] = 7; // fits in 3 signed bits as -1 after truncation/reinterpretation

  EXPECT_EQ(vec.bits(), 3);
  EXPECT_THAT(vec.unpack(), ElementsAre(-1, -1, 1));
}

TYPED_TEST(packed_int_vector_test, required_bits_unsigned_static) {
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

TYPED_TEST(packed_int_vector_test, required_bits_signed_static) {
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

TYPED_TEST(packed_int_vector_test, required_bits_member) {
  using vec_type = typename TypeParam::template auto_type<int32_t>;
  vec_type vec(0, 4);

  vec[0] = -1;
  vec[1] = 3;
  vec[2] = -8;
  vec[3] = 0;

  EXPECT_EQ(vec.required_bits(), 4);
  EXPECT_EQ(vec.bits(), 4);
  EXPECT_THAT(vec.unpack(), ElementsAre(-1, 3, -8, 0));
}

TYPED_TEST(packed_int_vector_test, auto_push_back_grows_bit_width) {
  using vec_type = typename TypeParam::template auto_type<uint32_t>;
  vec_type vec(0);

  vec.push_back(0);
  EXPECT_EQ(vec.bits(), 0);
  EXPECT_THAT(vec.unpack(), ElementsAre(0));

  vec.push_back(1);
  EXPECT_EQ(vec.bits(), 1);
  EXPECT_THAT(vec.unpack(), ElementsAre(0, 1));

  vec.push_back(7);
  EXPECT_EQ(vec.bits(), 3);
  EXPECT_THAT(vec.unpack(), ElementsAre(0, 1, 7));

  vec.push_back(31);
  EXPECT_EQ(vec.bits(), 5);
  EXPECT_THAT(vec.unpack(), ElementsAre(0, 1, 7, 31));
}

TYPED_TEST(packed_int_vector_test, auto_set_grows_bit_width) {
  using vec_type = typename TypeParam::template auto_type<uint32_t>;
  vec_type vec(0, 3);

  EXPECT_EQ(vec.bits(), 0);
  EXPECT_THAT(vec.unpack(), ElementsAre(0, 0, 0));

  vec[1] = 9;
  EXPECT_EQ(vec.bits(), 4);
  EXPECT_THAT(vec.unpack(), ElementsAre(0, 9, 0));

  vec[2] = 31;
  EXPECT_EQ(vec.bits(), 5);
  EXPECT_THAT(vec.unpack(), ElementsAre(0, 9, 31));
}

TYPED_TEST(packed_int_vector_test,
           auto_resize_grows_once_and_fills_new_values) {
  using vec_type = typename TypeParam::template auto_type<uint32_t>;
  vec_type vec(2);

  vec.push_back(1);
  vec.push_back(2);

  vec.resize(5, 17);

  EXPECT_EQ(vec.bits(), 5);
  EXPECT_EQ(vec.size(), 5);
  EXPECT_THAT(vec.unpack(), ElementsAre(1, 2, 17, 17, 17));
}

TYPED_TEST(packed_int_vector_test,
           auto_resize_shrink_does_not_change_bit_width) {
  using vec_type = typename TypeParam::template auto_type<uint32_t>;
  vec_type vec(2);

  vec.push_back(1);
  vec.push_back(2);
  vec.resize(5, 17);

  ASSERT_EQ(vec.bits(), 5);

  vec.resize(2);

  EXPECT_EQ(vec.bits(), 5);
  EXPECT_THAT(vec.unpack(), ElementsAre(1, 2));
}

TYPED_TEST(packed_int_vector_test, optimize_storage_shrinks_bit_width) {
  using vec_type = typename TypeParam::template auto_type<uint32_t>;
  vec_type vec(7);

  vec.push_back(3);
  vec.push_back(4);

  ASSERT_EQ(vec.bits(), 7);

  vec.optimize_storage();

  EXPECT_EQ(vec.bits(), 3);
  EXPECT_EQ(vec.required_bits(), 3);
  EXPECT_THAT(vec.unpack(), ElementsAre(3, 4));
}

TYPED_TEST(packed_int_vector_test, optimize_storage_can_reduce_to_zero_bits) {
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
  EXPECT_TRUE(vec.unpack() == std::vector<uint32_t>({0, 0}));
}

TYPED_TEST(packed_int_vector_test, truncate_to_bits_can_widen_and_narrow) {
  using vec_type = typename TypeParam::template auto_type<uint32_t>;
  vec_type vec(5);

  vec.push_back(1);
  vec.push_back(31);
  vec.push_back(7);

  vec.truncate_to_bits(8);
  EXPECT_EQ(vec.bits(), 8);
  EXPECT_THAT(vec.unpack(), ElementsAre(1, 31, 7));

  vec.truncate_to_bits(3);
  EXPECT_EQ(vec.bits(), 3);
  EXPECT_THAT(vec.unpack(), ElementsAre(1, 7, 7));
}

TYPED_TEST(packed_int_vector_test,
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
  EXPECT_THAT(vec.unpack(), ElementsAre(0, 0, 0, 0));
}

TYPED_TEST(packed_int_vector_test, invalid_bit_width_throws) {
  using vec_type = typename TypeParam::template type<uint32_t>;

  EXPECT_THROW((vec_type(33)), std::invalid_argument);
  EXPECT_THROW((vec_type(33, 1)), std::invalid_argument);

  vec_type vec(5);

  EXPECT_THROW(vec.reset(33, 0), std::invalid_argument);
  EXPECT_THROW(vec.truncate_to_bits(33), std::invalid_argument);
}

TYPED_TEST(packed_int_vector_test, policy_size_limit_throws_on_construction) {
  using vec_type = typename TypeParam::template type<uint32_t>;
  using size_type = typename vec_type::size_type;

  if constexpr (vec_type::max_size() < std::numeric_limits<size_type>::max()) {
    EXPECT_THROW((vec_type(0, vec_type::max_size() + 1)), std::length_error);
  }
}

TYPED_TEST(packed_int_vector_test, resize_throws_on_size_limit) {
  using vec_type = typename TypeParam::template type<uint32_t>;
  using size_type = typename vec_type::size_type;

  if constexpr (vec_type::max_size() < std::numeric_limits<size_type>::max()) {
    vec_type vec(0, vec_type::max_size());
    EXPECT_THROW(vec.resize(vec_type::max_size() + 1), std::length_error);
  }
}

TYPED_TEST(packed_int_vector_test, push_back_throws_on_size_limit) {
  using vec_type = typename TypeParam::template type<uint32_t>;
  using size_type = typename vec_type::size_type;

  if constexpr (vec_type::max_size() < std::numeric_limits<size_type>::max()) {
    vec_type vec(0, vec_type::max_size());
    EXPECT_THROW(vec.push_back(0), std::length_error);
  }
}

TYPED_TEST(packed_int_vector_test, reserve_throws_on_size_limit) {
  using vec_type = typename TypeParam::template type<uint32_t>;
  using size_type = typename vec_type::size_type;

  if constexpr (vec_type::max_size() < std::numeric_limits<size_type>::max()) {
    vec_type vec(0, vec_type::max_size());
    EXPECT_THROW(vec.reserve(vec_type::max_size() + 1), std::length_error);
  }
}

TYPED_TEST(packed_int_vector_test,
           policy_capacity_limit_throws_on_full_width_reserve) {
  using vec_type = typename TypeParam::template type<uint32_t>;
  using size_type = typename vec_type::size_type;

  using layout_type =
      detail::packed_vector_layout<typename vec_type::policy_type,
                                   typename vec_type::underlying_type>;

  if constexpr (layout_type::max_capacity_blocks_value <
                std::numeric_limits<size_type>::max()) {
    vec_type vec(std::numeric_limits<uint32_t>::digits);

    EXPECT_THROW(vec.reserve(layout_type::max_capacity_blocks_value + 1),
                 std::length_error);
  }
}

TYPED_TEST(packed_int_vector_test, auto_clear_preserves_bit_width) {
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
  EXPECT_THAT(vec.unpack(), ElementsAre(7));
}

TYPED_TEST(packed_int_vector_test, auto_mixed_operations_stress) {
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

TYPED_TEST(packed_int_vector_test, copy_constructor_and_assignment) {
  using vec_type = typename TypeParam::template type<uint32_t>;
  vec_type vec(5);

  vec.push_back(1);
  vec.push_back(31);
  vec.push_back(7);

  vec_type copy{vec};

  EXPECT_EQ(copy.bits(), 5);
  EXPECT_THAT(copy.unpack(), ElementsAre(1, 31, 7));

  copy[1] = 3;

  EXPECT_THAT(vec.unpack(), ElementsAre(1, 31, 7));
  EXPECT_THAT(copy.unpack(), ElementsAre(1, 3, 7));

  vec_type assigned(2);
  assigned = vec;

  EXPECT_EQ(assigned.bits(), 5);
  EXPECT_THAT(assigned.unpack(), ElementsAre(1, 31, 7));

  assigned[0] = 0;

  EXPECT_THAT(vec.unpack(), ElementsAre(1, 31, 7));
  EXPECT_THAT(assigned.unpack(), ElementsAre(0, 31, 7));
}

TYPED_TEST(packed_int_vector_test, move_constructor_and_assignment) {
  using vec_type = typename TypeParam::template type<uint32_t>;
  vec_type vec(5);

  vec.push_back(1);
  vec.push_back(31);
  vec.push_back(7);

  vec_type moved{std::move(vec)};

  EXPECT_EQ(moved.bits(), 5);
  EXPECT_THAT(moved.unpack(), ElementsAre(1, 31, 7));

  vec_type other(2);
  other.push_back(1);
  other.push_back(2);

  other = std::move(moved);

  EXPECT_EQ(other.bits(), 5);
  EXPECT_THAT(other.unpack(), ElementsAre(1, 31, 7));
}

TYPED_TEST(packed_int_vector_test, auto_copy_and_move_preserve_bit_width) {
  using vec_type = typename TypeParam::template auto_type<uint32_t>;
  vec_type vec(0);

  vec.push_back(1);
  vec.push_back(31);
  vec.push_back(7);

  EXPECT_EQ(vec.bits(), 5);

  vec_type copy{vec};
  EXPECT_EQ(copy.bits(), 5);
  EXPECT_THAT(copy.unpack(), ElementsAre(1, 31, 7));

  vec_type moved{std::move(copy)};
  EXPECT_EQ(moved.bits(), 5);
  EXPECT_THAT(moved.unpack(), ElementsAre(1, 31, 7));
}

TYPED_TEST(packed_int_vector_test,
           auto_repack_from_zero_bits_preserves_existing_zeros) {
  using vec_type = typename TypeParam::template auto_type<uint32_t>;
  vec_type vec(0, 4);

  EXPECT_EQ(vec.bits(), 0);
  EXPECT_EQ(vec.size_in_bytes(), 0);

  vec[2] = 7;

  EXPECT_EQ(vec.bits(), 3);
  EXPECT_THAT(vec.unpack(), ElementsAre(0, 0, 7, 0));
}

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

  EXPECT_THAT(vec.unpack(), ElementsAre(11, 31, 0, 5, 3, 7));

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
  EXPECT_THAT(vec.unpack(), ElementsAre(1, 2, 3, 4, 5, 6, 17, 17, 17, 17));

  vec.resize(5);

  EXPECT_EQ(vec.size(), 5);
  EXPECT_EQ(vec.segment_count(), 2);
  EXPECT_THAT(vec.unpack(), ElementsAre(1, 2, 3, 4, 5));

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
  EXPECT_THAT(vec.unpack(), ElementsAre(1, 2, 3, 4, 5, 6, 7, 8));

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
  EXPECT_THAT(vec.unpack(), ElementsAre(0, 31, 0, 0, 0, 0, 0, 0));

  vec[6] = 3;

  hist = vec.segment_bits_histogram();
  EXPECT_EQ(hist[0], 0);
  EXPECT_EQ(hist[2], 1);
  EXPECT_EQ(hist[5], 1);
  EXPECT_THAT(vec.unpack(), ElementsAre(0, 31, 0, 0, 0, 0, 3, 0));
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
  EXPECT_THAT(vec.unpack(), ElementsAre(0, 0, 0, 0, 0, 0, 3, 0));
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
  EXPECT_THAT(vec.unpack(), ElementsAre(7));
}

TEST(segmented_packed_int_vector, copy_constructor_and_assignment) {
  segmented_vec vec;

  for (uint32_t v : {1, 2, 3, 4, 31, 6}) {
    vec.push_back(v);
  }

  segmented_vec copy{vec};

  EXPECT_EQ(copy.segment_count(), vec.segment_count());
  EXPECT_EQ(copy.size_in_bytes(), vec.size_in_bytes());
  EXPECT_THAT(copy.unpack(), ElementsAreArray(vec.unpack()));

  copy[4] = 7;

  EXPECT_THAT(vec.unpack(), ElementsAre(1, 2, 3, 4, 31, 6));
  EXPECT_THAT(copy.unpack(), ElementsAre(1, 2, 3, 4, 7, 6));

  segmented_vec assigned;
  assigned = vec;

  EXPECT_EQ(assigned.segment_count(), vec.segment_count());
  EXPECT_EQ(assigned.size_in_bytes(), vec.size_in_bytes());
  EXPECT_THAT(assigned.unpack(), ElementsAreArray(vec.unpack()));
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
  EXPECT_THAT(moved.unpack(), ElementsAreArray(expected));

  segmented_vec other;
  other.push_back(99);
  other = std::move(moved);

  EXPECT_EQ(other.segment_count(), expected_segment_count);
  EXPECT_EQ(other.size_in_bytes(), expected_size_in_bytes);
  EXPECT_THAT(other.unpack(), ElementsAreArray(expected));
}

template <typename Vec>
class auto_packed_int_vector_test : public ::testing::Test {};

using exact_value_preserving_vector_types =
    ::testing::Types<auto_packed_int_vector<int64_t>,          //
                     compact_auto_packed_int_vector<int64_t>,  //
                     segmented_packed_int_vector<int64_t, 2>,  //
                     auto_packed_int_vector<uint32_t>,         //
                     compact_auto_packed_int_vector<uint32_t>, //
                     segmented_packed_int_vector<uint32_t, 8>, //
                     auto_packed_int_vector<uint16_t>,         //
                     compact_auto_packed_int_vector<uint16_t>, //
                     segmented_packed_int_vector<uint16_t, 4>, //
                     auto_packed_int_vector<int8_t>,           //
                     compact_auto_packed_int_vector<int8_t>,   //
                     segmented_packed_int_vector<int8_t, 16>>;

TYPED_TEST_SUITE(auto_packed_int_vector_test,
                 exact_value_preserving_vector_types);

TYPED_TEST(auto_packed_int_vector_test, mixed_operations_stress) {
  using value_type = typename TypeParam::value_type;
  using rng_value_type =
      std::conditional_t<sizeof(value_type) == 1, int, value_type>;

  for (auto const seed : seeds) {
    SCOPED_TRACE(::testing::Message() << "seed=" << seed);

    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> op_dist(0, 99);
    std::uniform_int_distribution<rng_value_type> value_dist(
        std::numeric_limits<value_type>::min(),
        std::numeric_limits<value_type>::max());

    TypeParam vec;
    std::vector<value_type> model;

    for (std::size_t step = 0; step < 5000; ++step) {
      int const op = op_dist(rng);

      SCOPED_TRACE(::testing::Message() << "step=" << step << ", op=" << op
                                        << ", size=" << model.size());

      if (op < 30) {
        value_type const value = value_dist(rng);
        vec.push_back(value);
        model.push_back(value);

      } else if (op < 45) {
        if (!model.empty()) {
          vec.pop_back();
          model.pop_back();
        }

      } else if (op < 65) {
        if (!model.empty()) {
          std::size_t const i = rng() % model.size();
          auto const value = value_dist(rng);
          vec[i] = value;
          model[i] = value;
        }

      } else if (op < 80) {
        std::size_t const new_size = rng() % 128;
        auto const fill_value = value_dist(rng);

        vec.resize(new_size, fill_value);
        model.resize(new_size, fill_value);

      } else if (op < 90) {
        vec.optimize_storage();

      } else {
        vec.clear();
        model.clear();
      }

      expect_matches_model(vec, model);
    }
  }
}

TYPED_TEST(auto_packed_int_vector_test, copy_and_move_smoke) {
  TypeParam vec;
  for (uint32_t v : {1, 2, 3, 31, 5, 6, 7}) {
    vec.push_back(v);
  }

  auto const expected = vec.unpack();

  TypeParam copy{vec};
  EXPECT_THAT(copy.unpack(), ElementsAreArray(expected));

  copy[3] = 9;
  EXPECT_THAT(vec.unpack(), ElementsAreArray(expected));

  TypeParam moved{std::move(copy)};
  EXPECT_EQ(moved.size(), expected.size());
}

TEST(compact_packed_int_vector, zero_bits_can_grow_inline_up_to_limit) {
  using vec_type = compact_packed_int_vector<uint32_t>;
  vec_type vec(0);

  auto const n = vec_type::inline_capacity_for_bits(vec.bits());

  vec.resize(n);

  EXPECT_EQ(vec.bits(), 0);
  EXPECT_EQ(vec.size(), n);
  EXPECT_TRUE(vec.is_inline());
  EXPECT_FALSE(vec.uses_heap());
  EXPECT_THAT(vec.unpack(), Each(0));

  vec.push_back(0);

  EXPECT_EQ(vec.bits(), 0);
  EXPECT_EQ(vec.size(), n + 1);
  EXPECT_FALSE(vec.is_inline());
  EXPECT_FALSE(vec.uses_heap());
  EXPECT_THAT(vec.unpack(), Each(0));
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
  EXPECT_THAT(vec.unpack(), ElementsAre(1, 1));
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

  EXPECT_THAT(vec.unpack(), ElementsAreArray(old));
  EXPECT_THAT(vec.capacity(), Ge(old.size()));
}

TYPED_TEST(packed_int_vector_type_test,
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
      EXPECT_THAT(at_boundary.unpack(), Each(typename vec_type::value_type{}));
      expect_representation_invariants(at_boundary);

      if (inline_cap < vec_type::max_size()) {
        vec_type over_boundary(bits, inline_cap + 1);
        EXPECT_FALSE(over_boundary.is_inline());
        EXPECT_EQ(over_boundary.bits(), bits);
        EXPECT_EQ(over_boundary.size(), inline_cap + 1);

        if (bits == 0) {
          EXPECT_FALSE(over_boundary.uses_heap());
          EXPECT_EQ(over_boundary.size_in_bytes(), 0);
        } else {
          EXPECT_TRUE(over_boundary.uses_heap());
          EXPECT_GT(over_boundary.size_in_bytes(), 0);
        }

        EXPECT_THAT(over_boundary.unpack(),
                    Each(typename vec_type::value_type{}));
        expect_representation_invariants(over_boundary);
      }
    }
  }
}

TYPED_TEST(packed_int_vector_type_test,
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
      EXPECT_FALSE(vec.uses_heap());
      EXPECT_EQ(vec.bits(), 0);
      EXPECT_EQ(vec.size(), 0);
      EXPECT_EQ(vec.size_in_bytes(), 0);
      EXPECT_GE(vec.capacity(), inline_cap + 1);
      expect_representation_invariants(vec);
    }
  }
}

TYPED_TEST(packed_int_vector_type_test,
           compact_shrink_to_fit_can_move_long_zero_bit_vector_back_inline) {
  using vec_type = typename TypeParam::type;

  if constexpr (vec_type::has_inline_storage) {
    auto const inline_cap = vec_type::inline_capacity_for_bits(0);
    ASSERT_LT(inline_cap, vec_type::max_size());

    vec_type vec(0, inline_cap + 1);

    ASSERT_FALSE(vec.is_inline());
    ASSERT_FALSE(vec.uses_heap());
    ASSERT_EQ(vec.bits(), 0);
    ASSERT_EQ(vec.size_in_bytes(), 0);
    expect_representation_invariants(vec);

    vec.resize(inline_cap);

    // resize alone should not necessarily change representation
    EXPECT_FALSE(vec.is_inline());
    EXPECT_FALSE(vec.uses_heap());
    EXPECT_EQ(vec.bits(), 0);
    EXPECT_EQ(vec.size(), inline_cap);
    expect_representation_invariants(vec);

    vec.shrink_to_fit();

    EXPECT_TRUE(vec.is_inline());
    EXPECT_FALSE(vec.uses_heap());
    EXPECT_EQ(vec.bits(), 0);
    EXPECT_EQ(vec.size(), inline_cap);
    EXPECT_EQ(vec.size_in_bytes(), 0);
    EXPECT_THAT(vec.unpack(), Each(typename vec_type::value_type{}));
    expect_representation_invariants(vec);
  }
}

TYPED_TEST(packed_int_vector_type_test,
           failing_limit_checks_leave_vector_unchanged) {
  using vec_type = typename TypeParam::type;
  using value_type = typename vec_type::value_type;
  using size_type = typename vec_type::size_type;

  if constexpr (vec_type::max_size() == std::numeric_limits<size_type>::max()) {
    SUCCEED();
  } else {
    vec_type vec(2);
    vec.push_back(static_cast<value_type>(1));
    vec.push_back(static_cast<value_type>(1));
    vec.push_back(static_cast<value_type>(0));

    auto const expected = vec.unpack();
    auto const old_bits = vec.bits();
    auto const old_size = vec.size();
    auto const old_size_in_bytes = vec.size_in_bytes();
    auto const was_inline = vec.is_inline();
    auto const used_heap = vec.uses_heap();

    constexpr auto too_big = vec_type::max_size() + 1;

    EXPECT_THROW(vec.reserve(too_big), std::length_error);
    EXPECT_EQ(vec.bits(), old_bits);
    EXPECT_EQ(vec.size(), old_size);
    EXPECT_EQ(vec.size_in_bytes(), old_size_in_bytes);
    EXPECT_EQ(vec.is_inline(), was_inline);
    EXPECT_EQ(vec.uses_heap(), used_heap);
    EXPECT_THAT(vec.unpack(), ElementsAreArray(expected));
    expect_representation_invariants(vec);

    EXPECT_THROW(vec.resize(too_big, static_cast<value_type>(1)),
                 std::length_error);
    EXPECT_EQ(vec.bits(), old_bits);
    EXPECT_EQ(vec.size(), old_size);
    EXPECT_EQ(vec.size_in_bytes(), old_size_in_bytes);
    EXPECT_EQ(vec.is_inline(), was_inline);
    EXPECT_EQ(vec.uses_heap(), used_heap);
    EXPECT_THAT(vec.unpack(), ElementsAreArray(expected));
    expect_representation_invariants(vec);
  }
}

TYPED_TEST(packed_int_vector_type_test,
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

      ASSERT_TRUE(a.is_inline());
      ASSERT_FALSE(a.uses_heap());
      ASSERT_FALSE(b.is_inline());
      ASSERT_TRUE(b.uses_heap());

      a.swap(b);

      EXPECT_THAT(a.unpack(), ElementsAreArray(b_expected));
      EXPECT_THAT(b.unpack(), ElementsAreArray(a_expected));

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

      ASSERT_TRUE(a.is_inline());
      ASSERT_FALSE(a.uses_heap());
      ASSERT_FALSE(b.is_inline());
      ASSERT_FALSE(b.uses_heap());

      a.swap(b);

      EXPECT_THAT(a.unpack(), ElementsAreArray(b_expected));
      EXPECT_THAT(b.unpack(), ElementsAreArray(a_expected));

      EXPECT_FALSE(a.is_inline());
      EXPECT_FALSE(a.uses_heap());
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

      ASSERT_FALSE(a.is_inline());
      ASSERT_TRUE(a.uses_heap());
      ASSERT_FALSE(b.is_inline());
      ASSERT_FALSE(b.uses_heap());

      a.swap(b);

      EXPECT_THAT(a.unpack(), ElementsAreArray(b_expected));
      EXPECT_THAT(b.unpack(), ElementsAreArray(a_expected));

      EXPECT_FALSE(a.is_inline());
      EXPECT_FALSE(a.uses_heap());
      EXPECT_FALSE(b.is_inline());
      EXPECT_TRUE(b.uses_heap());

      expect_representation_invariants(a);
      expect_representation_invariants(b);
    }
  }
}

TYPED_TEST(packed_int_vector_type_test,
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

TYPED_TEST(packed_int_vector_type_test,
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

TYPED_TEST(packed_int_vector_type_test,
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

TYPED_TEST(packed_int_vector_type_test,
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
