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
#include <cstddef>
#include <functional>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

#include <dwarfs/container/dense_index_hash.h>
#include <dwarfs/container/detail/dense_index_core.h>

namespace dwarfs::container {

template <typename T>
struct dense_map_index_policy_base {
  using key_type = typename T::first_type;
  using store_type = std::vector<T>;
  using hash_type = default_value_hash<key_type>;
  using equal_type = std::equal_to<>;
};

template <typename T>
struct std_dense_map_index_policy : dense_map_index_policy_base<T> {
  template <typename Hash, typename Equal>
  using index_type = std::unordered_set<std::size_t, Hash, Equal>;
};

/**
 * Map-like view onto an externally owned store of key-value pairs,
 * with unique keys.
 */
template <typename Key, typename Value, template <typename> typename Policy>
class basic_dense_map_index
    : private detail::dense_index_core<std::pair<Key, Value>,
                                       detail::pair_first_projection, Policy> {
  using core_type =
      detail::dense_index_core<std::pair<Key, Value>,
                               detail::pair_first_projection, Policy>;

  static_assert(std::same_as<typename core_type::key_type, Key>);

  template <typename K>
  static constexpr bool is_key_probe =
      core_type::template is_compatible_probe<K>;

  template <typename K>
  static constexpr bool is_insertable_key =
      is_key_probe<K> && std::constructible_from<Key, K const&>;

  // Stores with proxy references can be read, but not mutated
  static constexpr bool store_yields_references =
      std::is_reference_v<typename core_type::value_reference>;

  [[nodiscard]] static constexpr decltype(auto)
  project_mapped(auto&& value) noexcept {
    return detail::pair_second_projection{}(
        std::forward<decltype(value)>(value));
  }

 public:
  using policy_type = typename core_type::policy_type;
  using key_type = Key;
  using mapped_type = Value;
  using value_type = std::pair<Key, Value>;
  using size_type = typename core_type::size_type;
  using store_type = typename core_type::store_type;
  using hash_type = typename core_type::hash_type;
  using equal_type = typename core_type::equal_type;
  using insert_result = typename core_type::insert_result;
  using const_reference = typename core_type::const_value_reference;
  using const_key_reference = typename core_type::const_key_reference;
  using const_mapped_reference =
      typename core_type::template const_projected_reference<
          detail::pair_second_projection>;
  using mapped_reference = typename core_type::template projected_reference<
      detail::pair_second_projection>;

  explicit basic_dense_map_index(store_type& store,
                                 equal_type equal = equal_type{},
                                 hash_type hash = hash_type{})
      : core_type{store, std::move(equal), std::move(hash),
                  "basic_dense_map_index"} {}

  using core_type::contains;
  using core_type::empty;
  using core_type::index_capacity_in_bytes;
  using core_type::index_of;
  using core_type::index_size_in_bytes;
  using core_type::reserve;
  using core_type::size;
  using core_type::values;

  [[nodiscard]] const_reference operator[](size_type index) const noexcept {
    return core_type::value(index);
  }

  [[nodiscard]] const_reference at(size_type index) const {
    return core_type::value_at(index);
  }

  template <typename K>
    requires(is_key_probe<K>)
  [[nodiscard]] const_mapped_reference mapped_at(K const& key) const {
    return project_mapped(core_type::value(index_of_existing(key)));
  }

  template <typename K>
    requires(is_key_probe<K> && store_yields_references)
  [[nodiscard]] mapped_reference mapped_at(K const& key) {
    return project_mapped(core_type::mutable_value(index_of_existing(key)));
  }

  template <typename K>
    requires(is_insertable_key<K> && std::constructible_from<Value> &&
             store_yields_references)
  mapped_reference mapped(K const& key) {
    return project_mapped(core_type::mutable_value(try_emplace(key).index));
  }

  template <typename... Args>
  insert_result emplace(Args&&... args) {
    return core_type::emplace_value(std::forward<Args>(args)...);
  }

  template <typename K, typename... Args>
    requires(is_insertable_key<K> && std::constructible_from<Value, Args...>)
  insert_result try_emplace(K const& key, Args&&... args) {
    return core_type::emplace_value_if_absent(
        key, std::piecewise_construct, std::forward_as_tuple(key),
        std::forward_as_tuple(std::forward<Args>(args)...));
  }

  template <typename K, typename V>
    requires(is_insertable_key<K> && store_yields_references)
  insert_result insert_or_assign(K const& key, V&& value) {
    if (auto const ix = core_type::index_of(key)) {
      project_mapped(core_type::mutable_value(*ix)) = std::forward<V>(value);
      return {.index = *ix, .inserted = false};
    }

    return core_type::append_value(
        std::piecewise_construct, std::forward_as_tuple(key),
        std::forward_as_tuple(std::forward<V>(value)));
  }

  template <typename... Args>
  size_type add(Args&&... args) {
    return emplace(std::forward<Args>(args)...).index;
  }

 private:
  template <typename K>
  [[nodiscard]] size_type index_of_existing(K const& key) const {
    if (auto const ix = core_type::index_of(key)) {
      return *ix;
    }
    throw std::out_of_range{"basic_dense_map_index::mapped_at"};
  }
};

template <typename Key, typename Value>
using dense_map_index =
    basic_dense_map_index<Key, Value, std_dense_map_index_policy>;

} // namespace dwarfs::container
