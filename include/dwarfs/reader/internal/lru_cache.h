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

#include <functional>
#include <iterator>
#include <list>

#include <parallel_hashmap/phmap.h>

namespace dwarfs::reader::internal {

template <typename KeyT, typename T>
class lru_cache {
 public:
  using key_type = KeyT;
  using mapped_type = T;
  using value_type = std::pair<key_type const, mapped_type>;

  using iterator = typename std::list<value_type>::iterator;
  using const_iterator = typename std::list<value_type>::const_iterator;

  using prune_hook_type = std::function<void(key_type, mapped_type&&)>;

  lru_cache() = default;

  explicit lru_cache(size_t max_size)
      : max_size_{max_size} {
    index_.reserve(max_size_);
  }

  // Set the maximum cache size
  void set_max_size(size_t max_size) {
    max_size_ = max_size;
    while (cache_.size() > max_size_) {
      evict_lru();
    }
    index_.reserve(max_size_);
  }

  // Set a custom prune hook
  void set_prune_hook(prune_hook_type hook) { prune_hook_ = std::move(hook); }

  // Insert or update an item in the cache, promoting it
  void set(key_type const& key, mapped_type value,
           prune_hook_type custom_prune_hook = {}) {
    auto it = index_.find(key);
    if (it != index_.end()) {
      it->second->second = std::move(value);
      move_to_front(it->second);
    } else {
      if (index_.size() >= max_size_) {
        evict_lru(std::move(custom_prune_hook));
      }
      cache_.push_front(value_type(key, std::move(value)));
      index_[key] = cache_.begin();
    }
  }

  // Find an item, optionally promoting it
  iterator find(key_type const& key, bool promote = true) {
    auto it = index_.find(key);
    if (it == index_.end()) {
      return end();
    }
    if (promote) {
      move_to_front(it->second);
    }
    return it->second;
  }

  iterator erase(iterator pos, prune_hook_type custom_prune_hook = {}) {
    auto& key = pos->first;
    auto& value = pos->second;
    if (custom_prune_hook) {
      custom_prune_hook(key, std::move(value));
    } else if (prune_hook_) {
      prune_hook_(key, std::move(value));
    }
    index_.erase(key);
    return cache_.erase(pos);
  }

  void clear() {
    index_.clear();
    cache_.clear();
  }

  bool empty() const { return cache_.empty(); }

  size_t size() const { return cache_.size(); }

  iterator begin() { return cache_.begin(); }
  iterator end() { return cache_.end(); }

  const_iterator begin() const { return cache_.begin(); }
  const_iterator end() const { return cache_.end(); }

 private:
  // Move the accessed item to the front of the cache (most recently used)
  void move_to_front(iterator it) { cache_.splice(cache_.begin(), cache_, it); }

  // Evict the least recently used item
  void evict_lru(prune_hook_type custom_prune_hook = {}) {
    if (auto it = cache_.end(); it != cache_.begin()) {
      erase(--it, std::move(custom_prune_hook));
    }
  }

  size_t max_size_;
  phmap::flat_hash_map<key_type, iterator> index_;
  std::list<value_type> cache_;
  prune_hook_type prune_hook_;
};

} // namespace dwarfs::reader::internal
