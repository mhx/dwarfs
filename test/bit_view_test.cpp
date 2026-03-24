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
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <random>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <dwarfs/bit_view.h>

namespace {

using namespace dwarfs;

using ::testing::ElementsAreArray;

constexpr std::array<std::uint8_t, 64> kReferenceBytes = {
    0x00, 0xFF, 0xA5, 0x5A, 0x3C, 0xC3, 0xF0, 0x0F, 0x12, 0x34, 0x56,
    0x78, 0x9A, 0xBC, 0xDE, 0xF0, 0x11, 0x22, 0x44, 0x88, 0xEE, 0xDD,
    0xBB, 0x77, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0xFE,
    0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x10, 0x0B, 0xAD, 0xF0, 0x0D,
    0xCA, 0xFE, 0xBE, 0xEF, 0x13, 0x37, 0xC0, 0xDE, 0x55, 0xAA, 0x99,
    0x66, 0x7E, 0xE7, 0x18, 0x81, 0x2D, 0xD2, 0x4B, 0xB4,
};

constexpr auto
bitpos(std::size_t bit_index) -> std::pair<std::size_t, unsigned> {
  std::size_t const byte_index = bit_index / 8;
  unsigned const bit_in_byte = bit_index % 8;
  return {byte_index, bit_in_byte};
}

constexpr auto reference_bit(std::size_t bit_index) -> bool {
  auto const [byte_index, bit_in_byte] = bitpos(bit_index);
  return ((kReferenceBytes[byte_index] >> bit_in_byte) & 0x1) != 0;
}

template <typename T>
constexpr auto bit_mask(std::size_t width) -> std::make_unsigned_t<T> {
  using U = std::make_unsigned_t<T>;
  constexpr auto bits = std::numeric_limits<U>::digits;

  if (width == 0) {
    return U{0};
  }
  if (width >= bits) {
    return static_cast<U>(~U{0});
  }
  return static_cast<U>((U{1} << width) - U{1});
}

template <typename T>
constexpr auto
expected_read_from_reference(std::size_t bit_offset, std::size_t bit_width)
    -> T {
  using U = std::make_unsigned_t<T>;
  constexpr auto digits = std::numeric_limits<U>::digits;

  assert(bit_width <= digits);

  if (bit_width == 0) {
    return T{0};
  }

  U v = 0;

  for (std::size_t i = 0; i < bit_width; ++i) {
    bool const b = reference_bit(bit_offset + i);
    v |= (static_cast<U>(b) << i);
  }

  v &= bit_mask<T>(bit_width);

  if constexpr (std::signed_integral<T>) {
    std::size_t const unused = digits - bit_width;
    return (static_cast<T>(v << unused)) >> unused;
  } else {
    return static_cast<T>(v);
  }
}

template <typename Storage>
constexpr bool is_byte_storage_v =
    std::is_same_v<std::remove_cv_t<Storage>, std::byte> ||
    (std::is_unsigned_v<Storage> && sizeof(Storage) == 1);

template <typename Storage, typename T>
constexpr bool supports_value_type_v =
    std::integral<std::remove_cv_t<T>> &&
    !std::is_same_v<std::remove_cv_t<T>, bool> &&
    (is_byte_storage_v<Storage> || (sizeof(T) == sizeof(Storage)));

template <typename Storage>
auto storage_bytes(std::vector<Storage> const& storage)
    -> std::vector<std::uint8_t> {
  auto const b = std::as_bytes(std::span{storage});
  std::vector<std::uint8_t> out;
  out.reserve(b.size());
  for (std::byte x : b) {
    out.push_back(std::to_integer<std::uint8_t>(x));
  }
  return out;
}

template <typename T>
auto apply_expected_write(std::vector<std::uint8_t>& bytes, bit_range r,
                          T value) -> void {
  using U = std::make_unsigned_t<T>;
  U const u = static_cast<U>(value) & bit_mask<T>(r.bit_width);

  for (std::size_t i = 0; i < r.bit_width; ++i) {
    bool const bitval = ((u >> i) & U{1}) != 0;
    auto const [byte_index, bit_in_byte] = bitpos(r.bit_offset + i);

    auto const m = static_cast<std::uint8_t>(1U << bit_in_byte);
    if (bitval) {
      bytes[byte_index] |= m;
    } else {
      bytes[byte_index] &= ~m;
    }
  }
}

inline auto apply_expected_set(std::vector<std::uint8_t>& bytes,
                               std::size_t bit_index) -> void {
  auto const [byte_index, bit_in_byte] = bitpos(bit_index);
  bytes[byte_index] = static_cast<std::uint8_t>(
      bytes[byte_index] | (std::uint8_t{1} << bit_in_byte));
}

inline auto apply_expected_clear(std::vector<std::uint8_t>& bytes,
                                 std::size_t bit_index) -> void {
  auto const [byte_index, bit_in_byte] = bitpos(bit_index);
  bytes[byte_index] = static_cast<std::uint8_t>(
      bytes[byte_index] &
      ~static_cast<std::uint8_t>(std::uint8_t{1} << bit_in_byte));
}

template <typename T>
auto required_bytes_for_access(std::size_t bit_offset, std::size_t bit_width)
    -> std::size_t {
  using U = std::make_unsigned_t<T>;
  std::size_t const chunk = sizeof(U);

  if (bit_width == 0) {
    return 0;
  }

  auto const [byte0, bit_in_byte] = bitpos(bit_offset);
  std::size_t const chunk0_byte = byte0 & ~(chunk - 1);

  unsigned const shift = (byte0 - chunk0_byte) * 8 + bit_in_byte;

  std::size_t const chunk_bits = chunk * 8;
  bool const need_two =
      (static_cast<std::size_t>(shift) + bit_width) > chunk_bits;

  return chunk0_byte + (need_two ? 2 * chunk : chunk);
}

template <typename Storage>
auto make_storage_from_reference() -> std::vector<Storage> {
  static_assert(kReferenceBytes.size() % sizeof(Storage) == 0);
  std::vector<Storage> storage(kReferenceBytes.size() / sizeof(Storage));
  std::memcpy(storage.data(), kReferenceBytes.data(), kReferenceBytes.size());
  return storage;
}

template <typename T>
auto interesting_offsets(std::size_t type_bits) -> std::vector<std::size_t> {
  std::vector<std::size_t> off;
  std::size_t const small_sweep = std::min<std::size_t>(32, type_bits);
  for (std::size_t i = 0; i < small_sweep; ++i) {
    off.push_back(i);
  }

  for (std::size_t x : {type_bits > 0 ? type_bits - 1 : 0, type_bits,
                        type_bits + 1, type_bits + 7}) {
    // TODO: C++23: use std::ranges::contains
    if (std::ranges::find(off, x) == off.end()) {
      off.push_back(x);
    }
  }
  return off;
}

template <typename T>
auto min_value_for(std::size_t width) -> T {
  if constexpr (std::unsigned_integral<T>) {
    return T{0};
  } else {
    constexpr std::size_t digits =
        std::numeric_limits<std::make_unsigned_t<T>>::digits;
    assert(width <= digits);
    if (width == digits) {
      return std::numeric_limits<T>::min();
    }
    return static_cast<T>(-(std::int64_t{1} << (width - 1)));
  }
}

template <typename T>
auto max_value_for(std::size_t width) -> T {
  if constexpr (std::unsigned_integral<T>) {
    return bit_mask<T>(width);
  } else {
    constexpr std::size_t digits =
        std::numeric_limits<std::make_unsigned_t<T>>::digits;
    assert(width <= digits);
    if (width == digits) {
      return std::numeric_limits<T>::max();
    }
    return static_cast<T>((std::int64_t{1} << (width - 1)) - 1);
  }
}

template <typename T>
auto random_values_for(std::mt19937_64& rng, std::size_t width, int count)
    -> std::vector<T> {
  // MSVC doesn't like byte types for uniform_int_distribution
  using int_dist_t =
      std::conditional_t<std::is_signed_v<T>, std::int64_t, std::uint64_t>;
  std::vector<T> out(count);
  std::uniform_int_distribution<int_dist_t> dist(min_value_for<T>(width),
                                                 max_value_for<T>(width));
  std::ranges::generate(out, [&] { return static_cast<T>(dist(rng)); });
  return out;
}

template <typename Storage>
auto minimal_elements_for_bytes(std::size_t byte_count) -> std::size_t {
  if (byte_count == 0) {
    return 1; // keep vectors non-empty for convenience
  }
  return (byte_count + sizeof(Storage) - 1) / sizeof(Storage);
}

} // namespace

template <typename Storage>
class bit_view_test : public ::testing::Test {};

using storage_types = ::testing::Types<std::byte, std::uint8_t, std::uint16_t,
                                       std::uint32_t, std::uint64_t>;

TYPED_TEST_SUITE(bit_view_test, storage_types);

TYPED_TEST(bit_view_test, reads_match_reference_pattern) {
  using storage_t = TypeParam;

  auto storage = make_storage_from_reference<storage_t>();
  auto const* p = storage.data();
  auto view = bit_view(p);

  static_assert(!requires { view.set(0); });
  static_assert(!requires { view.clear(0); });
  static_assert(!requires { view.write(bit_range{0, 1}, std::uint8_t{0}); });

  for (std::size_t bit = 0; bit < kReferenceBytes.size() * 8; ++bit) {
    SCOPED_TRACE(bit);
    EXPECT_EQ(view.test(bit), reference_bit(bit));
  }

  std::array<bit_range, 10> const ranges = {{
      {0, 1},
      {1, 7},
      {7, 9},
      {8, 8},
      {13, 12},
      {29, 10},
      {31, 1},
      {33, 17},
      {53, 34},
      {120, 10},
  }};

  auto check_reads_for = [&](auto tag) {
    using T = decltype(tag);
    if constexpr (supports_value_type_v<storage_t, T>) {
      for (auto r : ranges) {
        constexpr std::size_t digits =
            std::numeric_limits<std::make_unsigned_t<T>>::digits;
        if (r.bit_width == 0 || r.bit_width > digits) {
          continue;
        }
        if (r.bit_offset + r.bit_width > kReferenceBytes.size() * 8) {
          continue;
        }

        SCOPED_TRACE((std::string{"T="} + typeid(T).name()));
        SCOPED_TRACE(r.bit_offset);
        SCOPED_TRACE(r.bit_width);

        T const expected =
            expected_read_from_reference<T>(r.bit_offset, r.bit_width);
        T const actual = view.template read<T>(r);
        EXPECT_EQ(actual, expected);
      }
    }
  };

  check_reads_for(std::uint8_t{});
  check_reads_for(std::uint16_t{});
  check_reads_for(std::uint32_t{});
  check_reads_for(std::uint64_t{});

  check_reads_for(std::int8_t{});
  check_reads_for(std::int16_t{});
  check_reads_for(std::int32_t{});
  check_reads_for(std::int64_t{});
}

TYPED_TEST(bit_view_test, bit_set_and_clear_only_touch_target_bit) {
  using storage_t = TypeParam;

  std::array<std::size_t, 10> const bits = {
      {0, 1, 7, 8, 9, 15, 16, 31, 32, 63}};

  for (std::size_t bit_index : bits) {
    auto const needed_bytes = (bit_index >> 3) + 1;
    auto const elems = minimal_elements_for_bytes<storage_t>(needed_bytes);

    {
      std::vector<storage_t> storage(elems);
      std::memset(storage.data(), 0x00, storage.size() * sizeof(storage_t));

      auto view = bit_view(storage.data());
      view.set(bit_index);

      auto expected =
          std::vector<std::uint8_t>(storage.size() * sizeof(storage_t), 0x00);
      apply_expected_set(expected, bit_index);

      EXPECT_THAT(storage_bytes(storage), ElementsAreArray(expected));
      EXPECT_TRUE(view.test(bit_index));

      view.set(bit_index);
      EXPECT_THAT(storage_bytes(storage), ElementsAreArray(expected));
    }

    {
      std::vector<storage_t> storage(elems);
      std::memset(storage.data(), 0xFF, storage.size() * sizeof(storage_t));

      auto view = bit_view(storage.data());
      view.clear(bit_index);

      auto expected =
          std::vector<std::uint8_t>(storage.size() * sizeof(storage_t), 0xFF);
      apply_expected_clear(expected, bit_index);

      EXPECT_THAT(storage_bytes(storage), ElementsAreArray(expected));
      EXPECT_FALSE(view.test(bit_index));

      view.clear(bit_index);
      EXPECT_THAT(storage_bytes(storage), ElementsAreArray(expected));
    }
  }
}

TYPED_TEST(bit_view_test, zero_width_bit_range_is_zero_and_noop) {
  using storage_t = TypeParam;

  // large offset with a 1-element buffer should trigger ASAN on OOB
  bit_range const zero_width{123456, 0};

  std::vector<storage_t> storage(1);
  std::memset(storage.data(), 0xA5, sizeof(storage_t));
  auto const before = storage_bytes(storage);

  // read() should return 0 for any supported type.
  auto const_view = const_bit_view(storage.data());

  auto check_read_zero = [&](auto tag) {
    using T = decltype(tag);
    if constexpr (supports_value_type_v<storage_t, T>) {
      EXPECT_EQ((const_view.template read<T>(zero_width)), T{0});
    }
  };

  check_read_zero(std::uint8_t{});
  check_read_zero(std::uint16_t{});
  check_read_zero(std::uint32_t{});
  check_read_zero(std::uint64_t{});
  check_read_zero(std::int8_t{});
  check_read_zero(std::int16_t{});
  check_read_zero(std::int32_t{});
  check_read_zero(std::int64_t{});

  // write() with width==0 should do nothing (memory unchanged).
  auto view = bit_view(storage.data());

  auto check_write_noop = [&](auto tag) {
    using T = decltype(tag);
    if constexpr (supports_value_type_v<storage_t, T>) {
      view.write(zero_width, static_cast<T>(0x7B));
      EXPECT_THAT(storage_bytes(storage), ElementsAreArray(before));
    }
  };

  check_write_noop(std::uint8_t{});
  check_write_noop(std::uint16_t{});
  check_write_noop(std::uint32_t{});
  check_write_noop(std::uint64_t{});
  check_write_noop(std::int8_t{});
  check_write_noop(std::int16_t{});
  check_write_noop(std::int32_t{});
  check_write_noop(std::int64_t{});
}

TYPED_TEST(bit_view_test, write_preserves_bits_outside_range) {
  using storage_t = TypeParam;

  auto exercise = [&](auto tag) {
    using T = decltype(tag);

    if constexpr (supports_value_type_v<storage_t, T>) {
      constexpr std::size_t digits =
          std::numeric_limits<std::make_unsigned_t<T>>::digits;
      auto const type_bits = sizeof(std::make_unsigned_t<T>) * 8;

      std::array<std::size_t, 8> const widths = {{
          1,
          2,
          7,
          8,
          std::min<std::size_t>(digits - 3, 17),
          std::min<std::size_t>(digits - 2, 31),
          digits - 1,
          digits,
      }};

      auto const offsets = interesting_offsets<T>(type_bits);

      for (std::size_t width : widths) {
        if (width == 0 || width > digits) {
          continue;
        }

        T const value = max_value_for<T>(width);

        for (std::size_t off : offsets) {
          bit_range const r{off, width};
          auto const need_bytes =
              required_bytes_for_access<T>(r.bit_offset, r.bit_width);
          auto const elems = minimal_elements_for_bytes<storage_t>(need_bytes);
          std::vector<storage_t> storage(elems);

          for (std::uint8_t fill : {std::uint8_t{0x00}, std::uint8_t{0xFF}}) {
            std::memset(storage.data(), fill,
                        storage.size() * sizeof(storage_t));

            auto view = bit_view(storage.data());
            view.write(r, value);

            std::vector<std::uint8_t> expected(
                storage.size() * sizeof(storage_t), fill);
            apply_expected_write(expected, r, value);

            auto const actual = storage_bytes(storage);

            SCOPED_TRACE((std::string{"Storage="} + typeid(storage_t).name()));
            SCOPED_TRACE((std::string{"T="} + typeid(T).name()));
            SCOPED_TRACE(fill);
            SCOPED_TRACE(r.bit_offset);
            SCOPED_TRACE(r.bit_width);

            EXPECT_THAT(actual, ElementsAreArray(expected));
          }
        }
      }
    }
  };

  exercise(std::uint8_t{});
  exercise(std::uint16_t{});
  exercise(std::uint32_t{});
  exercise(std::uint64_t{});

  exercise(std::int8_t{});
  exercise(std::int16_t{});
  exercise(std::int32_t{});
  exercise(std::int64_t{});
}

TYPED_TEST(bit_view_test, write_read_roundtrip) {
  using storage_t = TypeParam;

  std::mt19937_64 rng(42);

  auto exercise = [&](auto tag) {
    using T = decltype(tag);
    if constexpr (supports_value_type_v<storage_t, T>) {
      constexpr std::size_t digits =
          std::numeric_limits<std::make_unsigned_t<T>>::digits;
      auto const type_bits = sizeof(T) * 8;
      auto const offsets = interesting_offsets<T>(type_bits);

      for (std::size_t width = 1; width <= digits; ++width) {
        auto const randval = random_values_for<T>(rng, width, 3);
        std::vector<T> values;
        values.push_back(min_value_for<T>(width));
        values.push_back(max_value_for<T>(width));
        values.insert(values.end(), randval.begin(), randval.end());

        for (std::size_t off : offsets) {
          bit_range const r{off, width};
          auto const need_bytes =
              required_bytes_for_access<T>(r.bit_offset, r.bit_width);
          auto const elems = minimal_elements_for_bytes<storage_t>(need_bytes);
          std::vector<storage_t> storage(elems);

          std::memset(storage.data(), 0x55, storage.size() * sizeof(storage_t));
          auto view = bit_view(storage.data());

          for (T v : values) {
            view.write(r, v);
            auto const got = view.template read<T>(r);

            SCOPED_TRACE((std::string{"Storage="} + typeid(storage_t).name()));
            SCOPED_TRACE((std::string{"T="} + typeid(T).name()));
            SCOPED_TRACE(r.bit_offset);
            SCOPED_TRACE(r.bit_width);

            EXPECT_EQ(got, v);
          }
        }
      }
    }
  };

  exercise(std::uint8_t{});
  exercise(std::uint16_t{});
  exercise(std::uint32_t{});
  exercise(std::uint64_t{});

  exercise(std::int8_t{});
  exercise(std::int16_t{});
  exercise(std::int32_t{});
  exercise(std::int64_t{});
}

TEST(bit_view_asan_test, std_byte_uint32_last_block_only) {
  std::vector<std::byte> buf(12);
  std::memset(buf.data(), 0, buf.size());

  auto view = bit_view(buf.data());
  bit_range const r{8 * 8 + 3, 20};
  view.write(r, std::uint32_t{0xABCDE});
  EXPECT_EQ(view.read<std::uint32_t>(r),
            (std::uint32_t{0xABCDE} & bit_mask<std::uint32_t>(20)));
}

TEST(bit_view_asan_test, uint8_t_uint32_last_block_only) {
  std::vector<std::uint8_t> buf(12);
  std::memset(buf.data(), 0, buf.size());

  auto view = bit_view(buf.data());
  bit_range const r{8 * 8 + 3, 20};
  view.write(r, std::uint32_t{0xABCDE});
  EXPECT_EQ(view.read<std::uint32_t>(r),
            (std::uint32_t{0xABCDE} & bit_mask<std::uint32_t>(20)));
}
