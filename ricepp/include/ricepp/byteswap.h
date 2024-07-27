/* vim:set ts=2 sw=2 sts=2 et: */
/**
 * \author     Marcus Holland-Moritz (github@mhxnet.de)
 * \copyright  Copyright (c) Marcus Holland-Moritz
 *
 * This file is part of ricepp.
 *
 * ricepp is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * ricepp is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with ricepp.  If not, see <https://www.gnu.org/licenses/>.
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
