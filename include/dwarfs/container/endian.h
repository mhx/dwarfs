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

namespace dwarfs::container {

template <std::endian Endian, std::integral T>
  requires(!std::same_as<T, bool>)
constexpr auto convert(T value) noexcept -> T {
  if constexpr (std::endian::native == Endian || sizeof(T) == 1) {
    return value;
  } else {
    static_assert(sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8);
    return std::byteswap(value);
  }
}

template <std::integral T>
  requires(!std::same_as<T, bool>)
constexpr auto convert_endian(std::endian e, T value) noexcept -> T {
  if (e == std::endian::native || sizeof(T) == 1) {
    return value;
  }
  return std::byteswap(value);
}

} // namespace dwarfs::container
