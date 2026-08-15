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

#include <array>
#include <cstddef>

#include <dwarfs/container/detail/sorted_array_container.h>

namespace dwarfs::container {

namespace detail {

template <typename T>
constexpr inline bool is_std_array_v = false;

template <typename T, std::size_t N>
constexpr inline bool is_std_array_v<std::array<T, N>> = true;

} // namespace detail

template <typename Key, std::size_t N>
class sorted_array_set : public detail::sorted_array_container<Key, N> {
  using base_type = detail::sorted_array_container<Key, N>;

 public:
  using base_type::base_type;
};

// Inherited constructors do not produce deduction guides, so both forms have
// to be spelled out explicitly.
template <typename Key, std::size_t N>
sorted_array_set(std::array<Key, N>) -> sorted_array_set<Key, N>;

// Unlike the map, whose two guides are told apart by `std::pair` vs
// `std::array`, both set guides would accept a single `std::array` argument.
// A lone array therefore always means "these are the elements". Two or more
// arguments are always elements, so a set *of* arrays still deduces.
template <typename Key, typename... U>
  requires(sizeof...(U) > 0 || !detail::is_std_array_v<Key>)
sorted_array_set(Key, U...) -> sorted_array_set<Key, 1 + sizeof...(U)>;

} // namespace dwarfs::container
