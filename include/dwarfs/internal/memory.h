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

#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>

namespace dwarfs::internal {

template <typename T>
concept byte_like_type = std::same_as<std::remove_cv_t<T>, std::byte> ||
                         std::same_as<std::remove_cv_t<T>, char> ||
                         std::same_as<std::remove_cv_t<T>, unsigned char>;

template <typename T, byte_like_type U>
  requires(std::is_trivially_copyable_v<T> &&
           std::is_standard_layout_v<std::remove_cv_t<T>> &&
           !std::is_array_v<std::remove_cv_t<T>> &&
           !std::is_union_v<std::remove_cv_t<T>> && !std::is_volatile_v<T> &&
           !std::is_volatile_v<U> && std::is_const_v<T> == std::is_const_v<U>)
[[nodiscard]] auto as_aligned_ptr(U* p) -> T* {
  assert(reinterpret_cast<std::uintptr_t>(p) % alignof(T) == 0);
  return std::assume_aligned<alignof(T)>(reinterpret_cast<T*>(p));
}

} // namespace dwarfs::internal
