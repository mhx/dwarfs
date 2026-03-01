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
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <span>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <dwarfs/thrift_lite/varint.h>

using namespace dwarfs::thrift_lite;

namespace tl = dwarfs::thrift_lite;
using testing::ElementsAreArray;

namespace {

static_assert(tl::detail::byte_like_type<std::byte>);
static_assert(tl::detail::byte_like_type<char>);
static_assert(tl::detail::byte_like_type<signed char>);
static_assert(tl::detail::byte_like_type<unsigned char>);
static_assert(!tl::detail::byte_like_type<bool>);

static_assert(tl::varint_size<std::uint32_t>(0) == 1);
static_assert(tl::varint_size<std::uint32_t>(1) == 1);
static_assert(tl::varint_size<std::uint32_t>(127) == 1);
static_assert(tl::varint_size<std::uint32_t>(128) == 2);
static_assert(tl::varint_size<std::uint32_t>(16383) == 2);
static_assert(tl::varint_size<std::uint32_t>(16384) == 3);

static_assert(tl::max_varint_size<std::uint8_t> == 2);
static_assert(tl::max_varint_size<std::uint32_t> == 5);
static_assert(tl::max_varint_size<std::uint64_t> == 10);

static_assert(tl::zigzag_encode<std::int32_t>(0) == 0u);
static_assert(tl::zigzag_encode<std::int32_t>(-1) == 1u);
static_assert(tl::zigzag_encode<std::int32_t>(1) == 2u);
static_assert(tl::zigzag_decode<std::uint32_t>(0) == 0);
static_assert(tl::zigzag_decode<std::uint32_t>(1) == -1);
static_assert(tl::zigzag_decode<std::uint32_t>(2) == 1);
static_assert(tl::zigzag_decode<std::uint32_t>(0xFFFFFFFFu) ==
              std::numeric_limits<std::int32_t>::min());

static_assert(tl::max_varint_size<std::uint64_t> ==
              tl::varint_size(std::numeric_limits<std::uint64_t>::max()));
static_assert(tl::max_varint_size<std::uint32_t> ==
              tl::varint_size(std::numeric_limits<std::uint32_t>::max()));
static_assert(tl::max_varint_size<std::uint16_t> ==
              tl::varint_size(std::numeric_limits<std::uint16_t>::max()));
static_assert(tl::max_varint_size<std::uint8_t> ==
              tl::varint_size(std::numeric_limits<std::uint8_t>::max()));

template <std::unsigned_integral T, T value>
constexpr auto encode_constexpr_unsigned() {
  std::array<std::byte, tl::max_varint_size<T>> buf{};
  auto* out = buf.data();
  out = tl::varint_encode<T>(value, out);
  return std::pair{buf, static_cast<std::size_t>(out - buf.data())};
}

template <std::signed_integral T, T value>
constexpr auto encode_constexpr_signed() {
  using U = std::make_unsigned_t<T>;
  std::array<std::byte, tl::max_varint_size<U>> buf{};
  auto* out = buf.data();
  out = tl::varint_encode<T>(value, out);
  return std::pair{buf, static_cast<std::size_t>(out - buf.data())};
}

constexpr auto enc_u32_300 = encode_constexpr_unsigned<std::uint32_t, 300u>();
static_assert(enc_u32_300.second == 2);
static_assert(enc_u32_300.first[0] == static_cast<std::byte>(0xAC));
static_assert(enc_u32_300.first[1] == static_cast<std::byte>(0x02));

constexpr auto enc_i32_m1 = encode_constexpr_signed<std::int32_t, -1>();
static_assert(enc_i32_m1.second == 1);
static_assert(enc_i32_m1.first[0] == static_cast<std::byte>(0x01));

template <class T>
using unsigned_equiv_t =
    std::conditional_t<std::signed_integral<T>, std::make_unsigned_t<T>, T>;

template <class T>
std::vector<std::byte> encode_to_vector(T v) {
  using U = unsigned_equiv_t<T>;
  std::vector<std::byte> buf(tl::max_varint_size<U>);
  auto* out = buf.data();
  out = tl::varint_encode(v, out);
  buf.resize(static_cast<std::size_t>(out - buf.data()));
  return buf;
}

template <class It, class End, class T>
T decode_with_ec(It& it, End end, std::error_code& ec) {
  return tl::varint_decode<T>(it, end, ec);
}

std::vector<std::byte> make_bytes(std::initializer_list<std::uint8_t> init) {
  std::vector<std::byte> out(init.size());
  std::ranges::transform(init, out.begin(),
                         [](auto b) { return static_cast<std::byte>(b); });
  return out;
}

} // namespace

TEST(varint_error_code_test, error_code_enum_conversion_works) {
  // Requires your std::is_error_code_enum specialization + ADL find of
  // make_error_code().
  std::error_code ec = tl::varint_error::end_of_input;
  EXPECT_EQ(ec, tl::make_error_code(tl::varint_error::end_of_input));
  EXPECT_EQ(ec.category(), tl::varint_category());
}

TEST(varint_error_code_test, error_category) {
  auto const& cat = tl::varint_category();
  EXPECT_EQ(std::string(cat.name()), std::string("varint_error"));
  EXPECT_EQ(cat.message(static_cast<int>(tl::varint_error::none)), "no error");
  EXPECT_EQ(cat.message(999), "unknown varint error");
}

TEST(varint_size_test, varint_size_runtime_evaluation) {
  std::vector<std::pair<std::uint32_t, std::size_t>> const test_cases = {
      {0, 1},
      {1, 1},
      {127, 1},
      {128, 2},
      {16383, 2},
      {16384, 3},
      {(1 << 21) - 1, 3},
      {(1 << 21), 4},
      {std::numeric_limits<std::uint32_t>::max(), 5},
  };

  for (auto const& [v, expected_size] : test_cases) {
    EXPECT_EQ(expected_size, tl::varint_size(v)) << "v=" << v;
  }
}

TEST(varint_encode_test, encodes_known_unsigned_values) {
  EXPECT_THAT(encode_to_vector<std::uint32_t>(0),
              ElementsAreArray(make_bytes({0x00})));
  EXPECT_THAT(encode_to_vector<std::uint32_t>(1),
              ElementsAreArray(make_bytes({0x01})));
  EXPECT_THAT(encode_to_vector<std::uint32_t>(127),
              ElementsAreArray(make_bytes({0x7F})));
  EXPECT_THAT(encode_to_vector<std::uint32_t>(128),
              ElementsAreArray(make_bytes({0x80, 0x01})));
  EXPECT_THAT(encode_to_vector<std::uint32_t>(300),
              ElementsAreArray(make_bytes({0xAC, 0x02})));
}

TEST(varint_encode_test, encodes_max_uint32) {
  auto const v = std::numeric_limits<std::uint32_t>::max();
  EXPECT_THAT(encode_to_vector<std::uint32_t>(v),
              ElementsAreArray(make_bytes({0xFF, 0xFF, 0xFF, 0xFF, 0x0F})));
}

TEST(varint_encode_test, encodes_known_signed_values_via_zigzag) {
  // zigzag(-1)=1 -> 0x01
  EXPECT_THAT(encode_to_vector<std::int32_t>(-1),
              ElementsAreArray(make_bytes({0x01})));
  // zigzag(1)=2 -> 0x02
  EXPECT_THAT(encode_to_vector<std::int32_t>(1),
              ElementsAreArray(make_bytes({0x02})));

  // min int32 zigzags to 0xFFFFFFFF -> same encoding as max uint32
  auto const v = std::numeric_limits<std::int32_t>::min();
  EXPECT_THAT(encode_to_vector<std::int32_t>(v),
              ElementsAreArray(make_bytes({0xFF, 0xFF, 0xFF, 0xFF, 0x0F})));
}

TEST(varint_encode_test, encode_accepts_back_insert_iterator) {
  std::vector<std::byte> buf;
  tl::varint_encode<std::uint32_t>(300, std::back_inserter(buf));
  EXPECT_THAT(buf, ElementsAreArray(make_bytes({0xAC, 0x02})));
}

TEST(varint_decode_test, decodes_known_unsigned_values_and_advances_iterator) {
  auto buf = make_bytes({0xAC, 0x02}); // 300
  auto it = buf.begin();

  std::error_code ec;
  auto v = tl::varint_decode<std::uint32_t>(it, buf.end(), ec);

  EXPECT_FALSE(ec);
  EXPECT_EQ(v, 300u);
  EXPECT_EQ(it, buf.end());
}

TEST(varint_decode_test, clears_error_code_on_success) {
  auto buf = make_bytes({0x01});
  auto it = buf.begin();

  std::error_code ec = tl::make_error_code(tl::varint_error::result_overflow);
  auto v = tl::varint_decode<std::uint32_t>(it, buf.end(), ec);

  EXPECT_FALSE(ec);
  EXPECT_EQ(v, 1u);
}

TEST(varint_decode_test, accepts_overlong_encoding_by_default) {
  // 0 encoded as 0x80 0x00 (overlong). This implementation accepts it.
  auto buf = make_bytes({0x80, 0x00});
  auto it = buf.begin();

  std::error_code ec;
  auto v = tl::varint_decode<std::uint32_t>(it, buf.end(), ec);

  EXPECT_FALSE(ec);
  EXPECT_EQ(v, 0u);
  EXPECT_EQ(it, buf.end());
}

TEST(varint_decode_test, decodes_from_string_iterators_char_is_byte_like) {
  // 300 -> 0xAC 0x02
  std::string s;
  s.push_back(static_cast<char>(0xAC));
  s.push_back(static_cast<char>(0x02));

  auto it = s.begin();
  std::error_code ec;
  auto v = tl::varint_decode<std::uint32_t>(it, s.end(), ec);

  EXPECT_FALSE(ec);
  EXPECT_EQ(v, 300u);
  EXPECT_EQ(it, s.end());
}

TEST(varint_decode_test, decodes_with_counted_iterator_and_default_sentinel) {
  // Exercises sentinel_for<It> End with a different sentinel type.
  std::array<std::byte, 2> buf{static_cast<std::byte>(0xAC),
                               static_cast<std::byte>(0x02)};
  auto it = std::counted_iterator(buf.data(),
                                  static_cast<std::ptrdiff_t>(buf.size()));

  std::error_code ec;
  auto v = tl::varint_decode<std::uint32_t>(it, std::default_sentinel, ec);

  EXPECT_FALSE(ec);
  EXPECT_EQ(v, 300u);
  EXPECT_TRUE(it == std::default_sentinel);
}

TEST(varint_decode_test, decodes_signed_values_via_zigzag) {
  // -1 -> zigzag 1 -> 0x01
  auto buf = make_bytes({0x01});
  auto it = buf.begin();

  std::error_code ec;
  auto v = tl::varint_decode<std::int32_t>(it, buf.end(), ec);

  EXPECT_FALSE(ec);
  EXPECT_EQ(v, -1);
  EXPECT_EQ(it, buf.end());
}

TEST(varint_decode_test, roundtrip_many_values_unsigned) {
  std::vector<std::uint64_t> values = {
      0u,
      1u,
      2u,
      127u,
      128u,
      129u,
      300u,
      16383u,
      16384u,
      (1ull << 20) - 1,
      (1ull << 20),
      std::numeric_limits<std::uint32_t>::max(),
      std::numeric_limits<std::uint64_t>::max()};

  for (auto v : values) {
    auto buf = encode_to_vector<std::uint64_t>(v);
    auto it = buf.begin();
    std::error_code ec;
    auto decoded = tl::varint_decode<std::uint64_t>(it, buf.end(), ec);

    ASSERT_FALSE(ec) << "v=" << v;
    ASSERT_EQ(decoded, v) << "v=" << v;
    ASSERT_EQ(it, buf.end()) << "v=" << v;
  }
}

TEST(varint_decode_test, roundtrip_many_values_signed) {
  std::vector<std::int64_t> values = {
      0,
      1,
      -1,
      2,
      -2,
      63,
      -63,
      64,
      -64,
      8191,
      -8191,
      8192,
      -8192,
      std::numeric_limits<std::int32_t>::max(),
      std::numeric_limits<std::int32_t>::min(),
      std::numeric_limits<std::int64_t>::max(),
      std::numeric_limits<std::int64_t>::min(),
  };

  for (auto v : values) {
    auto buf = encode_to_vector<std::int64_t>(v);
    auto it = buf.begin();
    std::error_code ec;
    auto decoded = tl::varint_decode<std::int64_t>(it, buf.end(), ec);

    ASSERT_FALSE(ec) << "v=" << v;
    ASSERT_EQ(decoded, v) << "v=" << v;
    ASSERT_EQ(it, buf.end()) << "v=" << v;
  }
}

TEST(varint_decode_test, end_of_input_sets_error_and_does_not_advance_begin) {
  auto buf = make_bytes({0xAC}); // truncated encoding of 300 (needs 0xAC 0x02)
  auto it = buf.begin();

  std::error_code ec;
  auto v = tl::varint_decode<std::uint32_t>(it, buf.end(), ec);

  EXPECT_EQ(v, 0u);
  EXPECT_EQ(ec, tl::make_error_code(tl::varint_error::end_of_input));
  EXPECT_EQ(it, buf.begin()); // must not advance on failure
}

TEST(varint_decode_test,
     overflow_too_many_bytes_sets_error_and_does_not_advance_begin) {
  // For uint32_t, max_varint_size is 5. Provide 6 bytes with continuation bits
  // set.
  auto buf = make_bytes({0x80, 0x80, 0x80, 0x80, 0x80, 0x00});
  auto it = buf.begin();

  std::error_code ec;
  auto v = tl::varint_decode<std::uint32_t>(it, buf.end(), ec);

  EXPECT_EQ(v, 0u);
  EXPECT_EQ(ec, tl::make_error_code(tl::varint_error::result_overflow));
  EXPECT_EQ(it, buf.begin());
}

TEST(
    varint_decode_test,
    overflow_due_to_final_payload_width_sets_error_and_does_not_advance_begin) {
  // Last byte payload too wide for remaining bits:
  // shift=28 on the 5th byte; payload=0x1F has bit_width 5 => 28+5=33 > 32.
  auto buf = make_bytes({0xFF, 0xFF, 0xFF, 0xFF, 0x1F});
  auto it = buf.begin();

  std::error_code ec;
  auto v = tl::varint_decode<std::uint32_t>(it, buf.end(), ec);

  EXPECT_EQ(v, 0u);
  EXPECT_EQ(ec, tl::make_error_code(tl::varint_error::result_overflow));
  EXPECT_EQ(it, buf.begin());
}

TEST(varint_decode_test, uint8_boundary_success_and_overflow) {
  // 255 encodes as 0xFF 0x01 for uint8
  {
    auto buf = make_bytes({0xFF, 0x01});
    auto it = buf.begin();
    std::error_code ec;
    auto v = tl::varint_decode<std::uint8_t>(it, buf.end(), ec);
    EXPECT_FALSE(ec);
    EXPECT_EQ(v, static_cast<std::uint8_t>(255));
    EXPECT_EQ(it, buf.end());
  }

  // Overflow for uint8: second byte payload requires >1 remaining bit (shift=7)
  {
    auto buf = make_bytes({0xFF, 0x03}); // payload=3 => bit_width 2; 7+2=9 > 8
    auto it = buf.begin();
    std::error_code ec;
    auto v = tl::varint_decode<std::uint8_t>(it, buf.end(), ec);
    EXPECT_EQ(v, 0u);
    EXPECT_EQ(ec, tl::make_error_code(tl::varint_error::result_overflow));
    EXPECT_EQ(it, buf.begin());
  }
}

TEST(varint_decode_throwing_overload_test, does_not_throw_on_success) {
  auto buf = make_bytes({0xCF, 0x0F, 0x42, 0x00});
  auto it = buf.begin();

  EXPECT_NO_THROW({
    auto v = tl::varint_decode<std::int32_t>(it, buf.end());
    EXPECT_EQ(-1000, v);
    EXPECT_EQ(static_cast<std::byte>(0x42), *it);
    EXPECT_EQ(2, std::distance(buf.begin(), it));
  });
}

TEST(varint_decode_throwing_overload_test, throws_system_error_on_failure) {
  auto buf = make_bytes({0xAC}); // truncated
  auto it = buf.begin();

  try {
    (void)tl::varint_decode<std::uint32_t>(it, buf.end());
    FAIL() << "Expected std::system_error";
  } catch (std::system_error const& e) {
    EXPECT_EQ(e.code(), tl::make_error_code(tl::varint_error::end_of_input));
    EXPECT_EQ(e.code().category(), tl::varint_category());
    EXPECT_EQ(it, buf.begin());
  }
}
