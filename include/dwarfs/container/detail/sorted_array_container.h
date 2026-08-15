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
#include <concepts>
#include <cstddef>
#include <functional>
#include <ranges>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include <dwarfs/container/detail/compile_time_sort.h>

namespace dwarfs::container::detail {

/**
 * Extract the key from an element using the projection `Proj`.
 *
 * The member pointer case is spelled out rather than going through
 * `std::invoke` because this sits in the innermost loop of every lookup.
 */
template <auto Proj, typename T>
constexpr decltype(auto) project_key(T const& value) {
  if constexpr (std::is_member_object_pointer_v<decltype(Proj)>) {
    return (value.*Proj);
  } else {
    return Proj(value);
  }
}

/**
 * A type that can be compared against `Key` directly.
 *
 * Only the two operations the lookup algorithms actually perform are
 * required. `std::totally_ordered_with` would be the obvious spelling, but it
 * additionally demands a common reference type between `K` and `Key`, which
 * typically does not exist precisely when heterogeneous lookup is most
 * useful (e.g. a key type that is only explicitly constructible from `K`).
 */
template <typename K, typename Key>
concept comparable_key = requires(K const& k, Key const& key) {
  { key == k } -> std::convertible_to<bool>;
  { key < k } -> std::convertible_to<bool>;
};

/**
 * A type usable to look up `Key`, either by converting to it once or by
 * being compared against it directly.
 */
template <typename K, typename Key>
concept lookup_key =
    std::convertible_to<K const&, Key> || comparable_key<K, Key>;

/**
 * Shared implementation for `sorted_array_map` and `sorted_array_set`.
 *
 * Holds `N` elements of type `T` sorted by the key that `Proj` extracts from
 * them. `Proj` is a non-type parameter so that it can be a pointer to member
 * (`&std::pair<K, V>::first` for the map) or `std::identity` (for the set),
 * both of which double as range projections and therefore work unchanged with
 * `compile_time_sort`, `std::ranges::sort` and friends.
 */
template <typename T, std::size_t N, auto Proj = std::identity{}>
class sorted_array_container {
 public:
  using value_type = T;
  using key_type = std::remove_cvref_t<decltype(project_key<Proj>(
      std::declval<T const&>()))>;
  using const_iterator = std::array<value_type, N>::const_iterator;
  using const_reverse_iterator =
      std::array<value_type, N>::const_reverse_iterator;

  constexpr explicit sorted_array_container(std::array<value_type, N> data)
      : data_{sort(data)} {
    check_unique_keys();
  }

  template <typename... Elements>
    requires(sizeof...(Elements) == N) &&
            (std::constructible_from<value_type, Elements &&> && ...)
  constexpr explicit sorted_array_container(Elements&&... elements)
      : sorted_array_container{std::array<value_type, sizeof...(Elements)>{
            {std::forward<Elements>(elements)...}}} {}

  constexpr std::size_t size() const noexcept { return N; }

  constexpr bool empty() const noexcept { return N == 0; }

  template <lookup_key<key_type> K>
  constexpr bool contains(K const& k) const {
    return find(k) != data_.end();
  }

  template <lookup_key<key_type> K>
  constexpr std::size_t count(K const& k) const {
    return contains(k) ? 1 : 0;
  }

  template <lookup_key<key_type> K>
  constexpr const_iterator find(K const& k) const {
    // Prevent ambiguity in `key_of(v) == k` when `K` is convertible to
    // `key_type` and also heterogeneously comparable with it.
    if constexpr (std::convertible_to<K const&, key_type>) {
      return find_impl(static_cast<key_type const&>(k));
    } else {
      return find_impl(k);
    }
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

 protected:
  // Only derived classes get to copy, move or destroy one of these.
  sorted_array_container(sorted_array_container const&) = default;
  sorted_array_container(sorted_array_container&&) = default;
  sorted_array_container& operator=(sorted_array_container const&) = default;
  sorted_array_container& operator=(sorted_array_container&&) = default;
  ~sorted_array_container() = default;

 private:
  static constexpr decltype(auto) key_of(value_type const& v) {
    return project_key<Proj>(v);
  }

  template <typename K>
  constexpr const_iterator find_impl(K const& k) const {
    if constexpr (N <= 32) {
      return std::ranges::find_if(
          data_, [&k](value_type const& v) { return key_of(v) == k; });
    } else {
      auto it = std::lower_bound(
          data_.begin(), data_.end(), k,
          [](value_type const& v, K const& key) { return key_of(v) < key; });
      return it != data_.end() && key_of(*it) == k ? it : data_.end();
    }
  }

  static constexpr std::array<value_type, N>
  sort(std::array<value_type, N> arr) {
    if consteval {
      compile_time_sort(arr, std::ranges::less{}, Proj);
    } else {
      if (!std::ranges::is_sorted(arr, std::ranges::less{}, Proj)) {
        std::ranges::sort(arr, std::ranges::less{}, Proj);
      }
    }
    return arr;
  }

  constexpr void check_unique_keys() const {
    if (std::ranges::adjacent_find(data_, std::equal_to{}, Proj) !=
        data_.end()) {
      throw std::invalid_argument("Duplicate key");
    }
  }

  std::array<value_type, N> data_;
};

} // namespace dwarfs::container::detail
