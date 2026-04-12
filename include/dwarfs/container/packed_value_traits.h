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

#include <concepts>
#include <type_traits>
#include <utility>

namespace dwarfs::container {

template <typename T>
concept integral_but_not_bool = std::integral<T> && !std::same_as<T, bool>;

template <typename T>
struct packed_value_traits;

template <integral_but_not_bool T>
struct packed_value_traits<T> {
  using encoded_type = T;

  static constexpr encoded_type encode(T v) noexcept { return v; }

  static constexpr T decode(encoded_type v) noexcept { return v; }
};

template <typename E>
  requires std::is_enum_v<E>
struct packed_value_traits<E> {
  using encoded_type = std::underlying_type_t<E>;

  static constexpr encoded_type encode(E v) noexcept {
    return static_cast<encoded_type>(v);
  }

  static constexpr E decode(encoded_type v) noexcept {
    return static_cast<E>(v);
  }
};

} // namespace dwarfs::container
