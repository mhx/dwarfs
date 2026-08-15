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
#include <optional>
#include <stdexcept>
#include <utility>

#include <dwarfs/container/detail/sorted_array_container.h>

namespace dwarfs::container {

namespace detail {

template <typename Key, typename Value, std::size_t N>
using sorted_array_map_base =
    sorted_array_container<std::pair<Key, Value>, N,
                           &std::pair<Key, Value>::first>;

} // namespace detail

template <typename Key, typename Value, std::size_t N>
class sorted_array_map : public detail::sorted_array_map_base<Key, Value, N> {
  using base_type = detail::sorted_array_map_base<Key, Value, N>;

 public:
  using key_type = Key;
  using mapped_type = Value;
  using typename base_type::const_iterator;
  using typename base_type::const_reverse_iterator;
  using typename base_type::value_type;

  using base_type::base_type;

  template <detail::lookup_key<key_type> K>
  constexpr mapped_type const& operator[](K const& k) const {
    return at(k);
  }

  template <detail::lookup_key<key_type> K>
  constexpr mapped_type const& at(K const& k) const {
    if (auto it = this->find(k); it != this->end()) {
      return it->second;
    }

    throw std::out_of_range("Key not found");
  }

  template <detail::lookup_key<key_type> K>
  constexpr std::optional<mapped_type> get(K const& k) const {
    std::optional<mapped_type> result;

    if (auto it = this->find(k); it != this->end()) {
      result.emplace(it->second);
    }

    return result;
  }
};

// Inherited constructors do not produce deduction guides, so both forms have
// to be spelled out explicitly.
template <typename K, typename V, std::size_t N>
sorted_array_map(std::array<std::pair<K, V>, N>) -> sorted_array_map<K, V, N>;

template <typename K, typename V, typename... U>
sorted_array_map(std::pair<K, V>, U...)
    -> sorted_array_map<K, V, 1 + sizeof...(U)>;

} // namespace dwarfs::container
