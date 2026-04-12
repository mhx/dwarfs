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
#include <limits>

namespace dwarfs::container::detail {

template <std::unsigned_integral U>
consteval auto bit_width_for_max(U max_value) noexcept -> std::size_t {
  return max_value == 0 ? 1 : std::bit_width(max_value);
}

template <std::unsigned_integral U>
consteval auto max_for_bits(std::size_t bits) noexcept -> U {
  if (bits == 0) {
    return U{0};
  }
  if (bits >= std::numeric_limits<U>::digits) {
    return std::numeric_limits<U>::max();
  }
  return (U{1} << bits) - 1;
}

constexpr auto ceil_div(std::size_t n, std::size_t d) noexcept -> std::size_t {
  return d == 0 ? 0 : (n + d - 1) / d;
}

constexpr auto
round_up_to_multiple(std::size_t n, std::size_t m) noexcept -> std::size_t {
  return m == 0 || n == 0 ? n : ceil_div(n, m) * m;
}

consteval auto
saturating_mul(std::size_t a, std::size_t b) noexcept -> std::size_t {
  if (a == 0 || b == 0) {
    return 0;
  }
  if (a > std::numeric_limits<std::size_t>::max() / b) {
    return std::numeric_limits<std::size_t>::max();
  }
  return a * b;
}

} // namespace dwarfs::container::detail
