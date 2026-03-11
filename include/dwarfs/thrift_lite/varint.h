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
#include <iterator>
#include <limits>
#include <optional>
#include <system_error>
#include <type_traits>

#include <dwarfs/thrift_lite/detail/concepts.h>

namespace dwarfs::thrift_lite {

enum class varint_error {
  none,
  end_of_input,
  result_overflow,
};

std::error_category const& varint_category() noexcept;

inline auto make_error_code(varint_error e) noexcept -> std::error_code {
  return {static_cast<int>(e), varint_category()};
}

template <std::signed_integral T>
[[nodiscard]] constexpr auto
zigzag_encode(T const v) noexcept -> std::make_unsigned_t<T> {
  using U = std::make_unsigned_t<T>;
  auto const uv = static_cast<U>(v);
  return (uv << 1) ^ (U{} - (uv >> std::numeric_limits<T>::digits));
}

template <std::unsigned_integral T>
[[nodiscard]] constexpr auto
zigzag_decode(T const v) noexcept -> std::make_signed_t<T> {
  using S = std::make_signed_t<T>;
  return static_cast<S>((v >> 1) ^ static_cast<T>(T{} - (v & 1)));
}

template <std::unsigned_integral T>
[[nodiscard]] constexpr auto varint_size(T const v) noexcept -> std::size_t {
  auto const bw = std::bit_width(v);
  return bw == 0 ? 1 : (bw + 6) / 7;
}

template <std::unsigned_integral T>
constexpr inline std::size_t max_varint_size =
    (std::numeric_limits<T>::digits + 6) / 7;

template <std::unsigned_integral T>
constexpr auto
varint_encode(T v, std::output_iterator<std::byte> auto out) -> decltype(out) {
  while (v >= 0x80) {
    *out++ = static_cast<std::byte>((v & T{0x7f}) | T{0x80});
    v >>= 7;
  }
  *out++ = static_cast<std::byte>(v);
  return out;
}

template <std::signed_integral T>
constexpr auto
varint_encode(T v, std::output_iterator<std::byte> auto out) -> decltype(out) {
  return varint_encode(zigzag_encode(v), out);
}

template <std::unsigned_integral T, detail::byte_input_iterator It,
          std::sentinel_for<It> End>
[[nodiscard]] auto
varint_decode(It& begin, End const end, std::error_code& ec) -> T {
  T result{0};
  int shift{0};
  auto it = begin;

  for (std::size_t i = 0; i < max_varint_size<T>; ++i) {
    if (it == end) {
      ec = make_error_code(varint_error::end_of_input);
      return 0;
    }

    auto const byte = static_cast<std::uint8_t>(*it++);
    auto const payload = static_cast<std::uint8_t>(byte & 0x7f);
    result |= static_cast<T>(payload) << shift;

    if ((byte & 0x80) == 0) {
      if (std::bit_width(payload) + shift > std::numeric_limits<T>::digits) {
        // overflow
        break;
      }
      ec.clear();
      begin = it;
      return result;
    }

    shift += 7;
  }

  ec = make_error_code(varint_error::result_overflow);
  return 0;
}

template <std::signed_integral T, detail::byte_input_iterator It,
          std::sentinel_for<It> End>
[[nodiscard]] auto
varint_decode(It& it, End const end, std::error_code& ec) -> T {
  auto const uv = varint_decode<std::make_unsigned_t<T>>(it, end, ec);
  if (ec) {
    return 0;
  }
  return zigzag_decode(uv);
}

template <std::integral T, detail::byte_input_iterator It,
          std::sentinel_for<It> End>
[[nodiscard]] auto varint_decode(It& it, End const end) -> T {
  std::error_code ec;
  auto const result = varint_decode<T>(it, end, ec);
  if (ec) {
    throw std::system_error(ec);
  }
  return result;
}

} // namespace dwarfs::thrift_lite

namespace std {

template <>
struct is_error_code_enum<dwarfs::thrift_lite::varint_error> : std::true_type {
};

} // namespace std
