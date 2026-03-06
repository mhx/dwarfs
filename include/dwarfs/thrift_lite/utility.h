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

#if __cplusplus < 202002L
#error "C++20 support is required to use this library"
#endif

#include <concepts>
#include <type_traits>

namespace dwarfs::thrift_lite {

template <std::unsigned_integral T>
constexpr auto to_signed(T value) noexcept -> std::make_signed_t<T> {
  // Well defined since C++20
  return static_cast<std::make_signed_t<T>>(value);
}

namespace detail {

template <std::integral T>
class to_narrow_convertible {
 public:
  constexpr explicit to_narrow_convertible(T value) noexcept
      : value_{value} {}

  template <std::integral U>
    requires(std::is_signed_v<T> == std::is_signed_v<U>)
  constexpr explicit(false) operator U() const noexcept {
    return static_cast<U>(value_);
  }

 private:
  T value_;
};

} // namespace detail

template <std::integral T>
constexpr auto to_narrow(T value) noexcept -> detail::to_narrow_convertible<T> {
  return detail::to_narrow_convertible<T>{value};
}

} // namespace dwarfs::thrift_lite
