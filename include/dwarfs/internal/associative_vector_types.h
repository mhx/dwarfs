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
#include <utility>
#include <vector>

namespace dwarfs::internal {

template <typename Key>
class small_vector_set {
 public:
  using key_type = Key;
  using value_type = key_type;
  using size_type = std::size_t;
  using iterator = std::vector<value_type>::iterator;
  using const_iterator = std::vector<value_type>::const_iterator;

  auto begin() const -> const_iterator { return data_.begin(); }
  auto end() const -> const_iterator { return data_.end(); }

  auto size() const -> size_type { return data_.size(); }
  auto empty() const -> bool { return data_.empty(); }

  auto insert(key_type const& key) -> std::pair<iterator, bool> {
    auto it = std::ranges::find(data_, key);
    if (it != data_.end()) {
      return {it, false};
    }
    data_.push_back(key);
    return {data_.end() - 1, true};
  }

  bool contains(key_type const& key) const {
    return std::ranges::find(data_, key) != data_.end();
  }

 private:
  std::vector<value_type> data_;
};

template <typename Key, typename T>
class small_vector_map {
 public:
  using key_type = Key;
  using mapped_type = T;
  using value_type = std::pair<key_type const, mapped_type>;
  using size_type = std::size_t;
  using iterator = std::vector<value_type>::iterator;
  using const_iterator = std::vector<value_type>::const_iterator;

  auto begin() -> iterator { return data_.begin(); }
  auto end() -> iterator { return data_.end(); }
  auto begin() const -> const_iterator { return data_.begin(); }
  auto end() const -> const_iterator { return data_.end(); }

  auto size() const -> size_type { return data_.size(); }
  auto empty() const -> bool { return data_.empty(); }

  template <typename... Args>
  auto
  emplace(key_type const& key, Args&&... args) -> std::pair<iterator, bool> {
    auto it = find(key);
    if (it != data_.end()) {
      return {it, false};
    }
    data_.emplace_back(std::piecewise_construct, std::forward_as_tuple(key),
                       std::forward_as_tuple(std::forward<Args>(args)...));
    return {data_.end() - 1, true};
  }

  auto find(key_type const& key) -> iterator {
    return std::ranges::find_if(
        data_, [&key](value_type const& kv) { return kv.first == key; });
  }

  auto find(key_type const& key) const -> const_iterator {
    return std::ranges::find_if(
        data_, [&key](value_type const& kv) { return kv.first == key; });
  }

 private:
  std::vector<value_type> data_;
};

} // namespace dwarfs::internal
