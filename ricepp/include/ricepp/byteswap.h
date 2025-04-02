/* vim:set ts=2 sw=2 sts=2 et: */
/**
 * \author     Marcus Holland-Moritz (github@mhxnet.de)
 * \copyright  Copyright (c) Marcus Holland-Moritz
 *
 * This file is part of ricepp.
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

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>

#include <range/v3/algorithm/reverse.hpp>

#include <ricepp/detail/compiler.h>

namespace ricepp {

namespace detail {

template <std::unsigned_integral T>
[[nodiscard]] RICEPP_FORCE_INLINE constexpr T
byteswap_fallback(T value) noexcept {
  auto value_repr = std::bit_cast<std::array<std::byte, sizeof(T)>>(value);
  ranges::reverse(value_repr);
  return std::bit_cast<T>(value_repr);
}

template <std::unsigned_integral T>
[[nodiscard]] RICEPP_FORCE_INLINE constexpr T byteswap(T value) noexcept {
#if __cpp_lib_byteswap >= 202110L
  return std::byteswap(value);
#elif defined(__GNUC__) || defined(__clang__)
  static_assert(sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8);
  if constexpr (sizeof(T) == 2) {
    return __builtin_bswap16(value);
  } else if constexpr (sizeof(T) == 4) {
    return __builtin_bswap32(value);
  } else if constexpr (sizeof(T) == 8) {
    return __builtin_bswap64(value);
  }
#elif defined(_MSC_VER)
  static_assert(sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8);
  if (std::is_constant_evaluated()) {
    return byteswap_fallback(value);
  } else {
    if constexpr (sizeof(T) == 2) {
      return _byteswap_ushort(value);
    } else if constexpr (sizeof(T) == 4) {
      return _byteswap_ulong(value);
    } else if constexpr (sizeof(T) == 8) {
      return _byteswap_uint64(value);
    }
  }
#else
  return byteswap_fallback(value);
#endif
}

} // namespace detail

template <std::unsigned_integral T>
[[nodiscard]] RICEPP_FORCE_INLINE T byteswap(T value,
                                             std::endian byteorder) noexcept {
  static_assert(std::endian::native == std::endian::little ||
                std::endian::native == std::endian::big);
  if constexpr (sizeof(T) > 1) {
    if (byteorder != std::endian::native) {
      return detail::byteswap(value);
    }
  }
  return value;
}

template <std::endian byteorder, std::unsigned_integral T>
[[nodiscard]] RICEPP_FORCE_INLINE constexpr T byteswap(T value) noexcept {
  static_assert(std::endian::native == std::endian::little ||
                std::endian::native == std::endian::big);
  if constexpr (sizeof(T) > 1 && byteorder != std::endian::native) {
    return detail::byteswap(value);
  }
  return value;
}

} // namespace ricepp
