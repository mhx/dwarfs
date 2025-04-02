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

#include <algorithm>
#include <array>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <utility>

namespace dwarfs {

template <typename Key, typename Value, std::size_t N>
class sorted_array_map {
 public:
  using key_type = Key;
  using mapped_type = Value;
  using value_type = std::pair<key_type, mapped_type>;
  using const_iterator = typename std::array<value_type, N>::const_iterator;
  using const_reverse_iterator =
      typename std::array<value_type, N>::const_reverse_iterator;

  constexpr explicit sorted_array_map(std::array<value_type, N> data)
      : data_{sort(data)} {
    check_unique_keys();
  }

  template <typename... Pairs>
  constexpr explicit sorted_array_map(Pairs&&... pairs)
      : sorted_array_map{std::array<value_type, sizeof...(Pairs)>{
            {std::forward<Pairs>(pairs)...}}} {}

  constexpr std::size_t size() const noexcept { return N; }

  constexpr mapped_type const& operator[](key_type const& k) const {
    return at(k);
  }

  constexpr mapped_type const& at(key_type const& k) const {
    if (auto it = find(k); it != data_.end()) {
      return it->second;
    }

    throw std::out_of_range("Key not found");
  }

  constexpr std::optional<mapped_type> get(key_type const& k) const {
    std::optional<mapped_type> result;

    if (auto it = find(k); it != data_.end()) {
      result.emplace(it->second);
    }

    return result;
  }

  constexpr bool contains(key_type const& k) const {
    return find(k) != data_.end();
  }

  constexpr std::size_t count(key_type const& k) const {
    return contains(k) ? 1 : 0;
  }

  constexpr bool empty() const noexcept { return N == 0; }

  constexpr const_iterator find(key_type const& k) const {
    if constexpr (N <= 32) {
      if (auto it =
              std::find_if(data_.begin(), data_.end(),
                           [&k](value_type const& v) { return v.first == k; });
          it != data_.end()) {
        return it;
      }
    } else {
      if (auto it = std::lower_bound(
              data_.begin(), data_.end(), k,
              [](auto const& v, auto const& k) { return v.first < k; });
          it != data_.end() && it->first == k) {
        return it;
      }
    }

    return data_.end();
  }

  constexpr const_iterator begin() const noexcept { return data_.begin(); }
  constexpr const_iterator end() const noexcept { return data_.end(); }
  constexpr const_iterator cbegin() const noexcept { return data_.cbegin(); }
  constexpr const_iterator cend() const noexcept { return data_.cend(); }
  constexpr const_reverse_iterator rbegin() const noexcept {
    return data_.rbegin();
  }
  constexpr const_reverse_iterator rend() const noexcept {
    return data_.rend();
  }
  constexpr const_reverse_iterator crbegin() const noexcept {
    return data_.crbegin();
  }
  constexpr const_reverse_iterator crend() const noexcept {
    return data_.crend();
  }

 private:
  static constexpr std::array<value_type, N>
  sort(std::array<value_type, N> arr) {
    if (!std::ranges::is_sorted(arr, std::less{}, &value_type::first)) {
      std::ranges::sort(arr, std::less{}, &value_type::first);
    }
    return arr;
  }

  constexpr void check_unique_keys() const {
    if (std::ranges::adjacent_find(data_, std::equal_to{},
                                   &value_type::first) != data_.end()) {
      throw std::invalid_argument("Duplicate key");
    }
  }

  std::array<value_type, N> data_;
};

template <typename K, typename V, typename... U>
sorted_array_map(std::pair<K, V>, U...)
    -> sorted_array_map<K, V, 1 + sizeof...(U)>;

} // namespace dwarfs
