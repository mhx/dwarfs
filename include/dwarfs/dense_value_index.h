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

#include <cstddef>
#include <functional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <dwarfs/dense_index_hash.h>
#include <dwarfs/detail/dense_index_core.h>

namespace dwarfs {

template <typename T>
struct dense_value_index_policy_base {
  using store_type = std::vector<T>;
  using hash_type = default_value_hash<T>;
  using equal_type = std::equal_to<>;
};

template <typename T>
struct std_dense_value_index_policy : dense_value_index_policy_base<T> {
  template <typename Hash, typename Equal>
  using index_type = std::unordered_set<std::size_t, Hash, Equal>;
};

/**
 * Set-like view onto an externally owned store of unique values.
 */
template <typename T, template <typename> typename Policy>
class basic_dense_value_index
    : private detail::dense_index_core<T, detail::identity_key_projection,
                                       Policy> {
  using core_type =
      detail::dense_index_core<T, detail::identity_key_projection, Policy>;

 public:
  using policy_type = typename core_type::policy_type;
  using value_type = T;
  using size_type = typename core_type::size_type;
  using store_type = typename core_type::store_type;
  using hash_type = typename core_type::hash_type;
  using equal_type = typename core_type::equal_type;
  using const_reference = typename core_type::const_value_reference;
  using insert_result = typename core_type::insert_result;

  explicit basic_dense_value_index(store_type& store,
                                   equal_type equal = equal_type{},
                                   hash_type hash = hash_type{})
      : core_type{store, std::move(equal), std::move(hash),
                  "basic_dense_value_index"} {}

  using core_type::contains;
  using core_type::empty;
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

  template <typename... Args>
  insert_result emplace(Args&&... args) {
    return core_type::emplace_value(std::forward<Args>(args)...);
  }

  template <typename... Args>
  size_type add(Args&&... args) {
    return emplace(std::forward<Args>(args)...).index;
  }
};

template <typename T>
using dense_value_index =
    basic_dense_value_index<T, std_dense_value_index_policy>;

} // namespace dwarfs
