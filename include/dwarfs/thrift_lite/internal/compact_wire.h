/* vim:set ts=2 sw=2 sts=2 et: */
/**
 * \author     Marcus Holland-Moritz (github@mhxnet.de)
 * \copyright  Copyright (c) Marcus Holland-Moritz
 *
 * This file is part of dwarfs.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the “Software”), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

#include <dwarfs/thrift_lite/types.h>

namespace dwarfs::thrift_lite::internal {

// Compact type ids (low nibble)
constexpr inline std::uint8_t ct_stop = 0x00;
constexpr inline std::uint8_t ct_boolean_true = 0x01;
constexpr inline std::uint8_t ct_boolean_false = 0x02;
constexpr inline std::uint8_t ct_byte = 0x03;
constexpr inline std::uint8_t ct_i16 = 0x04;
constexpr inline std::uint8_t ct_i32 = 0x05;
constexpr inline std::uint8_t ct_i64 = 0x06;
constexpr inline std::uint8_t ct_double = 0x07;
constexpr inline std::uint8_t ct_binary = 0x08;
constexpr inline std::uint8_t ct_list = 0x09;
constexpr inline std::uint8_t ct_set = 0x0a;
constexpr inline std::uint8_t ct_map = 0x0b;
constexpr inline std::uint8_t ct_struct = 0x0c;
constexpr inline std::uint8_t ct_uuid = 0x0d;

template <std::signed_integral T>
[[nodiscard]] constexpr std::make_unsigned_t<T>
zigzag_encode(T const v) noexcept {
  using U = std::make_unsigned_t<T>;
  auto const uv = static_cast<U>(v);
  return (uv << 1) ^ -(uv >> std::numeric_limits<T>::digits);
}

template <std::unsigned_integral T>
[[nodiscard]] constexpr std::make_signed_t<T>
zigzag_decode(T const v) noexcept {
  using S = std::make_signed_t<T>;
  return static_cast<S>((v >> 1) ^ -(v & 1));
}

inline void write_u8(std::vector<std::byte>& out, std::uint8_t const b) {
  out.push_back(static_cast<std::byte>(b));
}

inline void write_bytes(std::vector<std::byte>& out, void const* const p,
                        std::size_t const n) {
  auto const* const bytes = static_cast<std::byte const*>(p);
  out.insert(out.end(), bytes, bytes + n);
}

template <std::unsigned_integral T>
void write_varint(std::vector<std::byte>& out, T v) {
  while (v >= 0x80) {
    write_u8(out, static_cast<std::uint8_t>(v) | 0x80);
    v >>= 7;
  }
  write_u8(out, static_cast<std::uint8_t>(v));
}

[[nodiscard]] std::uint8_t compact_type_id_for_field(ttype t);

[[nodiscard]] inline std::uint8_t
compact_type_id_for_collection(ttype const t) {
  if (t == ttype::bool_t) {
    return ct_boolean_true;
  }
  return compact_type_id_for_field(t);
}

} // namespace dwarfs::thrift_lite::internal
