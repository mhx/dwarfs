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

#include <cstdint>
#include <limits>
#include <random>
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
