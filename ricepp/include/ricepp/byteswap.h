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

namespace ricepp {

namespace detail {

template <std::unsigned_integral T>
[[nodiscard]] constexpr T byteswap(T value) noexcept {
#if __cpp_lib_byteswap >= 202110L
  return std::byteswap(value);
#else
  auto value_repr = std::bit_cast<std::array<std::byte, sizeof(T)>>(value);
  ranges::reverse(value_repr);
  return std::bit_cast<T>(value_repr);
#endif
}

} // namespace detail

template <std::unsigned_integral T>
[[nodiscard]] T byteswap(T value, std::endian byteorder) noexcept {
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
[[nodiscard]] constexpr T byteswap(T value) noexcept {
  static_assert(std::endian::native == std::endian::little ||
                std::endian::native == std::endian::big);
  if constexpr (sizeof(T) > 1 && byteorder != std::endian::native) {
    return detail::byteswap(value);
  }
  return value;
}

} // namespace ricepp
