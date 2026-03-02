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
#include <bit>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <dwarfs/endian.h>

namespace {

using namespace dwarfs;

template <class T>
consteval auto sample_u() -> std::make_unsigned_t<T> {
  if constexpr (sizeof(T) == 1) {
    return UINT8_C(0x5A);
  } else if constexpr (sizeof(T) == 2) {
    return UINT16_C(0x1234);
  } else if constexpr (sizeof(T) == 4) {
    return UINT32_C(0x11223344);
  } else {
    static_assert(sizeof(T) == 8);
    return UINT64_C(0x0102030405060708);
  }
}

template <class T>
consteval auto sample_value() -> T {
  return static_cast<T>(sample_u<T>());
}

template <class T>
consteval auto sample_bytes_le() -> std::array<std::uint8_t, sizeof(T)> {
  if constexpr (sizeof(T) == 1) {
    return {0x5A};
  } else if constexpr (sizeof(T) == 2) {
    return {0x34, 0x12};
  } else if constexpr (sizeof(T) == 4) {
    return {0x44, 0x33, 0x22, 0x11};
  } else {
    static_assert(sizeof(T) == 8);
    return {0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01};
  }
}

template <class T>
consteval auto sample_bytes_be() -> std::array<std::uint8_t, sizeof(T)> {
  if constexpr (sizeof(T) == 1) {
    return {0x5A};
  } else if constexpr (sizeof(T) == 2) {
    return {0x12, 0x34};
  } else if constexpr (sizeof(T) == 4) {
    return {0x11, 0x22, 0x33, 0x44};
  } else {
    static_assert(sizeof(T) == 8);
    return {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
  }
}

template <std::endian Endian, class T>
consteval auto sample_bytes() -> std::array<std::uint8_t, sizeof(T)> {
  if constexpr (Endian == std::endian::little) {
    return sample_bytes_le<T>();
  } else {
    static_assert(Endian == std::endian::big);
    return sample_bytes_be<T>();
  }
}

template <std::endian Endian, class T>
consteval auto expected_converted_value() -> T {
  return std::bit_cast<T>(sample_bytes<Endian, T>());
}

template <typename T>
constexpr auto bit_cast_array(T x) {
  return std::bit_cast<std::array<std::uint8_t, sizeof(T)>>(x);
}

} // namespace

static_assert(noexcept(dwarfs::convert<std::endian::little>(std::uint32_t{0})));
static_assert(noexcept(dwarfs::convert_endian(std::endian::little,
                                              std::uint32_t{0})));

static_assert(dwarfs::convert<std::endian::little>(std::uint32_t{1}) ==
              std::bit_cast<std::uint32_t>(std::array<std::uint8_t, 4>{
                  0x01, 0x00, 0x00, 0x00}));

static_assert(dwarfs::convert_endian(std::endian::little, std::uint32_t{1}) ==
              std::bit_cast<std::uint32_t>(std::array<std::uint8_t, 4>{
                  0x01, 0x00, 0x00, 0x00}));

static_assert(dwarfs::convert<std::endian::big>(std::uint32_t{1}) ==
              std::bit_cast<std::uint32_t>(std::array<std::uint8_t, 4>{
                  0x00, 0x00, 0x00, 0x01}));

static_assert(dwarfs::convert_endian(std::endian::big, std::uint32_t{1}) ==
              std::bit_cast<std::uint32_t>(std::array<std::uint8_t, 4>{
                  0x00, 0x00, 0x00, 0x01}));

static_assert(
    dwarfs::convert<std::endian::little>(sample_value<std::uint16_t>()) ==
    expected_converted_value<std::endian::little, std::uint16_t>());
static_assert(
    dwarfs::convert<std::endian::big>(sample_value<std::uint16_t>()) ==
    expected_converted_value<std::endian::big, std::uint16_t>());

static_assert(
    dwarfs::convert<std::endian::little>(sample_value<std::uint32_t>()) ==
    expected_converted_value<std::endian::little, std::uint32_t>());
static_assert(
    dwarfs::convert<std::endian::big>(sample_value<std::uint32_t>()) ==
    expected_converted_value<std::endian::big, std::uint32_t>());

static_assert(
    dwarfs::convert<std::endian::little>(sample_value<std::uint64_t>()) ==
    expected_converted_value<std::endian::little, std::uint64_t>());
static_assert(
    dwarfs::convert<std::endian::big>(sample_value<std::uint64_t>()) ==
    expected_converted_value<std::endian::big, std::uint64_t>());

static_assert(
    dwarfs::convert<std::endian::little>(sample_value<std::int32_t>()) ==
    expected_converted_value<std::endian::little, std::int32_t>());
static_assert(dwarfs::convert<std::endian::big>(sample_value<std::int32_t>()) ==
              expected_converted_value<std::endian::big, std::int32_t>());

static_assert([] {
  constexpr std::uint32_t v = 0x11223344u;
  constexpr dwarfs::uint32be_t x{v};

  static_assert(sizeof(x) == sizeof(std::uint32_t));
  static_assert(std::is_trivially_copyable_v<decltype(x)>);

  // stored bytes must be big-endian representation of v
  constexpr auto stored = bit_cast_array(x);
  static_assert(stored == std::array<std::uint8_t, 4>{0x11, 0x22, 0x33, 0x44});

  // public value must roundtrip
  static_assert(static_cast<std::uint32_t>(x) == v);
  static_assert(x.load() == v);

  // assignment + comparisons
  dwarfs::uint32be_t a{1};
  dwarfs::uint32be_t b{2};
  a = 3;

  return static_cast<std::uint32_t>(a) == 3 && (b < a) &&
         ((b <=> a) == std::strong_ordering::less);
}());

template <class T, std::endian Endian>
struct convert_param {
  using type = T;
  static constexpr std::endian endian = Endian;
};

template <class Param>
class convert_test : public ::testing::Test {};

using convert_params =
    ::testing::Types<convert_param<std::uint8_t, std::endian::little>,
                     convert_param<std::uint8_t, std::endian::big>,
                     convert_param<std::int8_t, std::endian::little>,
                     convert_param<std::int8_t, std::endian::big>,
                     convert_param<std::uint16_t, std::endian::little>,
                     convert_param<std::uint16_t, std::endian::big>,
                     convert_param<std::int16_t, std::endian::little>,
                     convert_param<std::int16_t, std::endian::big>,
                     convert_param<std::uint32_t, std::endian::little>,
                     convert_param<std::uint32_t, std::endian::big>,
                     convert_param<std::int32_t, std::endian::little>,
                     convert_param<std::int32_t, std::endian::big>,
                     convert_param<std::uint64_t, std::endian::little>,
                     convert_param<std::uint64_t, std::endian::big>,
                     convert_param<std::int64_t, std::endian::little>,
                     convert_param<std::int64_t, std::endian::big>>;

TYPED_TEST_SUITE(convert_test, convert_params);

TYPED_TEST(convert_test, converts_to_expected_bit_pattern_value) {
  using TT = typename TypeParam::type;
  constexpr auto endian = TypeParam::endian;

  TT const v = sample_value<TT>();
  TT const actual = dwarfs::convert<endian>(v);
  TT const actual2 = dwarfs::convert_endian(endian, v);

  TT const expected = expected_converted_value<endian, TT>();
  EXPECT_EQ(actual, expected);
  EXPECT_EQ(actual2, expected);

  auto const actual_bytes = bit_cast_array(actual);
  auto const expected_bytes = sample_bytes<endian, TT>();
  EXPECT_THAT(actual_bytes, ::testing::ElementsAreArray(expected_bytes));
}

template <class T, std::endian Endian>
struct boxed_param {
  using value_type = T;
  static constexpr std::endian endian = Endian;
  using boxed_type = dwarfs::boxed_endian<T, Endian>;
};

template <class Param>
class boxed_endian_test : public ::testing::Test {};

using boxed_params =
    ::testing::Types<boxed_param<std::uint16_t, std::endian::little>,
                     boxed_param<std::uint16_t, std::endian::big>,
                     boxed_param<std::uint32_t, std::endian::little>,
                     boxed_param<std::uint32_t, std::endian::big>,
                     boxed_param<std::uint64_t, std::endian::little>,
                     boxed_param<std::uint64_t, std::endian::big>>;

TYPED_TEST_SUITE(boxed_endian_test, boxed_params);

TYPED_TEST(boxed_endian_test,
           default_constructs_to_zero_and_stores_zero_pattern) {
  using value_type = typename TypeParam::value_type;
  using boxed_type = typename TypeParam::boxed_type;

  static_assert(sizeof(boxed_type) == sizeof(value_type));
  static_assert(std::is_trivially_copyable_v<boxed_type>);

  boxed_type x{};
  EXPECT_EQ(static_cast<value_type>(x), static_cast<value_type>(0));
  EXPECT_EQ(x.load(), static_cast<value_type>(0));

  auto const stored = bit_cast_array(x);
  auto const expected = bit_cast_array<value_type>(0);
  EXPECT_THAT(stored, ::testing::ElementsAreArray(expected));
}

TYPED_TEST(boxed_endian_test, stores_target_endian_bytes_and_roundtrips_value) {
  using value_type = typename TypeParam::value_type;
  using boxed_type = typename TypeParam::boxed_type;
  constexpr auto endian = TypeParam::endian;

  static_assert(sizeof(boxed_type) == sizeof(value_type));
  static_assert(std::is_trivially_copyable_v<boxed_type>);

  value_type const v = sample_value<value_type>();
  boxed_type const x{v};

  EXPECT_EQ(static_cast<value_type>(x), v);
  EXPECT_EQ(x.load(), v);

  auto const stored = bit_cast_array(x);
  auto const expected_bytes = sample_bytes<endian, value_type>();
  EXPECT_THAT(stored, ::testing::ElementsAreArray(expected_bytes));
}

TYPED_TEST(boxed_endian_test, assignment_overwrites_and_returns_reference) {
  using value_type = typename TypeParam::value_type;
  using boxed_type = typename TypeParam::boxed_type;

  boxed_type x{};
  value_type const v1 = sample_value<value_type>();
  value_type const v2 = static_cast<value_type>(v1 + 1);

  auto& r1 = (x = v1);
  EXPECT_EQ(&r1, &x);
  EXPECT_EQ(x.load(), v1);

  auto& r2 = (x = v2);
  EXPECT_EQ(&r2, &x);
  EXPECT_EQ(static_cast<value_type>(x), v2);
}

TYPED_TEST(boxed_endian_test, comparisons_use_logical_value) {
  using value_type = typename TypeParam::value_type;
  using boxed_type = typename TypeParam::boxed_type;

  boxed_type a{static_cast<value_type>(1)};
  boxed_type b{static_cast<value_type>(2)};

  EXPECT_TRUE(a == a);
  EXPECT_TRUE(a != b);
  EXPECT_LT(a, b);
  EXPECT_EQ((a <=> b), std::strong_ordering::less);
}
