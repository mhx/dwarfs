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
#include <random>
#include <stdexcept>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <dwarfs/internal/packed_int_vector.h>

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
using stress_vec_type = auto_packed_int_vector<stress_value_type>;

constexpr size_t stress_digits = std::numeric_limits<stress_value_type>::digits;

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

size_t required_bits_of(std::vector<stress_value_type> const& values) {
  size_t bits = 0;
  for (auto v : values) {
    bits = std::max(bits, stress_vec_type::required_bits(v));
  }
  return bits;
}

void expect_matches_model(stress_vec_type const& vec,
                          std::vector<stress_value_type> const& model,
                          size_t current_bits) {
  ASSERT_EQ(vec.size(), model.size());
  ASSERT_EQ(vec.bits(), current_bits);
  ASSERT_EQ(vec.required_bits(), required_bits_of(model));
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

} // namespace

TEST(packed_int_vector, basic) {
  packed_int_vector<uint32_t> vec(5);

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

  EXPECT_EQ(vec.capacity(), 6);

  EXPECT_EQ(vec[0], 11);
  EXPECT_FALSE(vec.empty());

  vec.clear();

  EXPECT_EQ(vec.size(), 0);
  EXPECT_TRUE(vec.empty());
  vec.shrink_to_fit();
  EXPECT_EQ(vec.capacity(), 0);
  EXPECT_EQ(vec.size_in_bytes(), 0);
}

TEST(packed_int_vector, signed_int) {
  packed_int_vector<int64_t> vec(13);

  for (int64_t i = -4096; i < 4096; ++i) {
    vec.push_back(i);
  }

  EXPECT_EQ(vec.size(), 8192);
  EXPECT_EQ(vec.size_in_bytes(), 13312);

  EXPECT_EQ(vec.front(), -4096);
  EXPECT_EQ(vec.back(), 4095);

  vec.resize(4096);

  for (int64_t i = 0; i < 4096; ++i) {
    EXPECT_EQ(vec[i], i - 4096);
  }

  auto unpacked = vec.unpack();

  for (int64_t i = 0; i < 4096; ++i) {
    EXPECT_EQ(unpacked[i], i - 4096);
  }
}

TEST(packed_int_vector, zero_bits) {
  packed_int_vector<uint32_t> vec(0);

  for (uint32_t i = 0; i < 100; ++i) {
    vec.push_back(0);
  }

  EXPECT_EQ(vec.size(), 100);
  EXPECT_EQ(vec.size_in_bytes(), 0);

  for (uint32_t i = 0; i < 100; ++i) {
    EXPECT_EQ(vec[i], 0);
  }
}

TEST(packed_int_vector, resize_grow_zero_initializes_new_elements) {
  packed_int_vector<uint32_t> vec(5);

  for (uint32_t v : {1, 31, 7, 9, 3, 25}) {
    vec.push_back(v);
  }

  vec.resize(4);
  vec.resize(6);

  EXPECT_THAT(vec.unpack(), ElementsAre(1, 31, 7, 9, 0, 0));
}

TEST(packed_int_vector, cross_block_round_trip) {
  packed_int_vector<uint32_t> vec(17);

  std::vector<uint32_t> values{0, 1, 17, 12345, 65535, 131071, 42, 99999};

  for (auto v : values) {
    vec.push_back(v);
  }

  EXPECT_THAT(vec.unpack(), ElementsAreArray(values));
}

TEST(packed_int_vector, full_width_unsigned_round_trip) {
  packed_int_vector<uint32_t> vec(32);

  std::vector<uint32_t> values{0, 1, 0x7fffffff, 0x80000000, 0xffffffff};

  for (auto v : values) {
    vec.push_back(v);
  }

  EXPECT_THAT(vec.unpack(), ElementsAreArray(values));
}

TEST(packed_int_vector, full_width_signed_round_trip) {
  packed_int_vector<int64_t> vec(64);

  std::vector<int64_t> values{std::numeric_limits<int64_t>::min(), -1, 0, 1,
                              std::numeric_limits<int64_t>::max()};

  for (auto v : values) {
    vec.push_back(v);
  }

  EXPECT_THAT(vec.unpack(), ElementsAreArray(values));
}

TEST(packed_int_vector, copy_is_independent) {
  packed_int_vector<uint32_t> vec(6);
  for (uint32_t v : {1, 2, 3, 4}) {
    vec.push_back(v);
  }

  auto copy = vec;
  copy[1] = 55;
  copy.push_back(12);

  EXPECT_THAT(vec.unpack(), ElementsAre(1, 2, 3, 4));
  EXPECT_THAT(copy.unpack(), ElementsAre(1, 55, 3, 4, 12));
}

TEST(packed_int_vector, reset_reinitializes_storage_and_metadata) {
  packed_int_vector<uint32_t> vec(5);
  for (uint32_t v : {1, 2, 3, 4}) {
    vec.push_back(v);
  }

  vec.reset(9, 3);

  EXPECT_EQ(vec.bits(), 9);
  EXPECT_EQ(vec.size(), 3);
  EXPECT_EQ(vec.size_in_bytes(), 4);
  EXPECT_THAT(vec.unpack(), ElementsAre(0, 0, 0));
}

TEST(packed_int_vector, reserve_preserves_contents) {
  packed_int_vector<uint32_t> vec(5);
  vec.push_back(7);
  vec.push_back(11);

  vec.reserve(100);

  EXPECT_THAT(vec.unpack(), ElementsAre(7, 11));
  EXPECT_THAT(vec.capacity(), Ge(100));
}

TEST(packed_int_vector, zero_bits_always_reads_as_zero) {
  packed_int_vector<uint32_t> vec(0);

  vec.push_back(123);
  vec.push_back(456);
  vec.resize(4);
  vec[1] = 999;

  EXPECT_EQ(vec.size(), 4);
  EXPECT_EQ(vec.size_in_bytes(), 0);
  EXPECT_THAT(vec.unpack(), Each(0));
}

TEST(packed_int_vector, stress_round_trip_unsigned) {
  using value_type = uint32_t;
  constexpr size_t digits = std::numeric_limits<value_type>::digits;

  std::mt19937_64 rng(4711);

  for (size_t bits = 0; bits <= digits; ++bits) {
    SCOPED_TRACE(::testing::Message() << "bits=" << bits);

    packed_int_vector<value_type> vec(bits);
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

TEST(packed_int_vector, stress_round_trip_signed) {
  using value_type = int32_t;
  constexpr size_t digits =
      std::numeric_limits<std::make_unsigned_t<value_type>>::digits;

  std::mt19937_64 rng(4711);

  for (size_t bits = 0; bits <= digits; ++bits) {
    SCOPED_TRACE(::testing::Message() << "bits=" << bits);

    packed_int_vector<value_type> vec(bits);
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

TEST(packed_int_vector, stress_mixed_operations_unsigned) {
  using value_type = uint32_t;
  constexpr size_t digits = std::numeric_limits<value_type>::digits;

  std::mt19937_64 rng(4711);

  for (size_t bits = 0; bits <= digits; ++bits) {
    SCOPED_TRACE(::testing::Message() << "bits=" << bits);

    packed_int_vector<value_type> vec(bits);
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

TEST(packed_int_vector,
     fixed_width_assignment_truncates_without_touching_neighbours) {
  packed_int_vector<uint32_t> vec(5);

  vec.push_back(1);
  vec.push_back(2);
  vec.push_back(3);

  vec[1] = 63; // 0b111111 -> truncated to 0b11111 == 31

  EXPECT_EQ(vec.bits(), 5);
  EXPECT_THAT(vec.unpack(), ElementsAre(1, 31, 3));
}

TEST(packed_int_vector,
     fixed_width_assignment_truncates_without_touching_cross_block_neighbours) {
  packed_int_vector<uint32_t> vec(5);

  // 7 elements at 5 bits each span more than one 32-bit block.
  for (uint32_t v : {1, 2, 3, 4, 5, 6, 7}) {
    vec.push_back(v);
  }

  vec[5] = 255; // truncated to 31

  EXPECT_EQ(vec.bits(), 5);
  EXPECT_THAT(vec.unpack(), ElementsAre(1, 2, 3, 4, 5, 31, 7));
}

TEST(packed_int_vector,
     fixed_width_signed_assignment_truncates_without_touching_neighbours) {
  packed_int_vector<int32_t> vec(3);

  vec.push_back(-1);
  vec.push_back(0);
  vec.push_back(1);

  vec[1] = 7; // fits in 3 signed bits as -1 after truncation/reinterpretation

  EXPECT_EQ(vec.bits(), 3);
  EXPECT_THAT(vec.unpack(), ElementsAre(-1, -1, 1));
}

TEST(packed_int_vector, required_bits_unsigned_static) {
  using vec_type = packed_int_vector<uint32_t>;

  EXPECT_EQ(vec_type::required_bits(0), 0);
  EXPECT_EQ(vec_type::required_bits(1), 1);
  EXPECT_EQ(vec_type::required_bits(2), 2);
  EXPECT_EQ(vec_type::required_bits(3), 2);
  EXPECT_EQ(vec_type::required_bits(4), 3);
  EXPECT_EQ(vec_type::required_bits(31), 5);
  EXPECT_EQ(vec_type::required_bits(32), 6);
  EXPECT_EQ(vec_type::required_bits(std::numeric_limits<uint32_t>::max()), 32);
}

TEST(packed_int_vector, required_bits_signed_static) {
  using vec_type = packed_int_vector<int32_t>;

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

TEST(packed_int_vector, required_bits_member) {
  auto_packed_int_vector<int32_t> vec(0, 4);

  vec[0] = -1;
  vec[1] = 3;
  vec[2] = -8;
  vec[3] = 0;

  EXPECT_EQ(vec.required_bits(), 4);
  EXPECT_EQ(vec.bits(), 4);
  EXPECT_THAT(vec.unpack(), ElementsAre(-1, 3, -8, 0));
}

TEST(packed_int_vector, auto_push_back_grows_bit_width) {
  auto_packed_int_vector<uint32_t> vec(0);

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

TEST(packed_int_vector, auto_set_grows_bit_width) {
  auto_packed_int_vector<uint32_t> vec(0, 3);

  EXPECT_EQ(vec.bits(), 0);
  EXPECT_THAT(vec.unpack(), ElementsAre(0, 0, 0));

  vec[1] = 9;
  EXPECT_EQ(vec.bits(), 4);
  EXPECT_THAT(vec.unpack(), ElementsAre(0, 9, 0));

  vec[2] = 31;
  EXPECT_EQ(vec.bits(), 5);
  EXPECT_THAT(vec.unpack(), ElementsAre(0, 9, 31));
}

TEST(packed_int_vector, auto_resize_grows_once_and_fills_new_values) {
  auto_packed_int_vector<uint32_t> vec(2);

  vec.push_back(1);
  vec.push_back(2);

  vec.resize(5, 17);

  EXPECT_EQ(vec.bits(), 5);
  EXPECT_EQ(vec.size(), 5);
  EXPECT_THAT(vec.unpack(), ElementsAre(1, 2, 17, 17, 17));
}

TEST(packed_int_vector, auto_resize_shrink_does_not_change_bit_width) {
  auto_packed_int_vector<uint32_t> vec(2);

  vec.push_back(1);
  vec.push_back(2);
  vec.resize(5, 17);

  ASSERT_EQ(vec.bits(), 5);

  vec.resize(2);

  EXPECT_EQ(vec.bits(), 5);
  EXPECT_THAT(vec.unpack(), ElementsAre(1, 2));
}

TEST(packed_int_vector, optimize_storage_shrinks_bit_width) {
  auto_packed_int_vector<uint32_t> vec(7);

  vec.push_back(3);
  vec.push_back(4);

  ASSERT_EQ(vec.bits(), 7);

  vec.optimize_storage();

  EXPECT_EQ(vec.bits(), 3);
  EXPECT_EQ(vec.required_bits(), 3);
  EXPECT_THAT(vec.unpack(), ElementsAre(3, 4));
}

TEST(packed_int_vector, optimize_storage_can_reduce_to_zero_bits) {
  auto_packed_int_vector<uint32_t> vec(5);

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

TEST(packed_int_vector, truncate_to_bits_can_widen_and_narrow) {
  packed_int_vector<uint32_t> vec(5);

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

TEST(packed_int_vector, truncate_to_bits_zero_clears_storage_for_zero_values) {
  packed_int_vector<uint32_t> vec(5, 4);

  EXPECT_EQ(vec.bits(), 5);
  EXPECT_EQ(vec.size(), 4);
  EXPECT_EQ(vec.size_in_bytes(), 4);

  vec.truncate_to_bits(0);

  EXPECT_EQ(vec.bits(), 0);
  EXPECT_EQ(vec.size(), 4);
  EXPECT_EQ(vec.size_in_bytes(), 0);
  EXPECT_THAT(vec.unpack(), ElementsAre(0, 0, 0, 0));
}

TEST(packed_int_vector, invalid_bit_width_throws) {
  EXPECT_THROW((packed_int_vector<uint32_t>(33)), std::invalid_argument);
  EXPECT_THROW((packed_int_vector<uint32_t>(33, 1)), std::invalid_argument);

  packed_int_vector<uint32_t> vec(5);
  EXPECT_THROW(vec.reset(33, 0), std::invalid_argument);
  EXPECT_THROW(vec.truncate_to_bits(33), std::invalid_argument);
}

TEST(packed_int_vector, auto_clear_preserves_bit_width) {
  auto_packed_int_vector<uint32_t> vec(5);

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

TEST(packed_int_vector, auto_mixed_operations_stress) {
  constexpr std::array seeds{
      0x123456789abcdef0ULL, 0xfedcba9876543210ULL, 0xdeadbeefdeadbeefULL,
      0x0badc0de0badc0deULL, 0xabcdef0123456789ULL, 0x0123456789abcdefULL,
  };

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
        current_bits = required_bits_of(model);

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
