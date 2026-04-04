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
#include <concepts>
#include <cstddef>
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>

namespace dwarfs::internal::detail {

struct default_block_growth_policy {
  constexpr std::size_t operator()(std::size_t current_capacity,
                                   std::size_t min_capacity) const noexcept {
    if (current_capacity >= min_capacity) {
      return current_capacity;
    }

    std::size_t new_capacity = current_capacity == 0 ? 1 : current_capacity;
    while (new_capacity < min_capacity) {
      if (new_capacity > std::numeric_limits<std::size_t>::max() / 2) {
        return min_capacity;
      }
      new_capacity *= 2;
    }
    return new_capacity;
  }
};

template <typename T, typename GrowthPolicy = default_block_growth_policy>
class block_storage {
  static_assert(std::is_trivially_copyable_v<T>);

 public:
  using value_type = T;
  using size_type = std::size_t;

  enum class initialization {
    no_init,
    zero_init,
  };

  block_storage() noexcept = default;

  explicit block_storage(size_type capacity_blocks,
                         initialization init = initialization::no_init)
      : data_{allocate(capacity_blocks, init)}
      , capacity_blocks_{capacity_blocks} {}

  block_storage(block_storage&&) noexcept = default;
  block_storage& operator=(block_storage&&) noexcept = default;

  block_storage(block_storage const&) = delete;
  block_storage& operator=(block_storage const&) = delete;

  value_type* data() noexcept { return data_.get(); }
  value_type const* data() const noexcept { return data_.get(); }

  size_type capacity() const noexcept { return capacity_blocks_; }
  bool empty() const noexcept { return capacity_blocks_ == 0; }

  void swap(block_storage& other) noexcept {
    using std::swap;
    swap(data_, other.data_);
    swap(capacity_blocks_, other.capacity_blocks_);
  }

  void reset(size_type capacity_blocks,
             initialization init = initialization::no_init) {
    block_storage tmp(capacity_blocks, init);
    swap(tmp);
  }

  void release() noexcept {
    data_.reset();
    capacity_blocks_ = 0;
  }

  void reserve(size_type used_blocks, size_type min_capacity_blocks) {
    if (min_capacity_blocks <= capacity_blocks_) {
      return;
    }

    auto const grown_capacity =
        GrowthPolicy{}(capacity_blocks_, min_capacity_blocks);
    reallocate_copy_prefix(used_blocks,
                           std::max(grown_capacity, min_capacity_blocks));
  }

  void reserve_exact(size_type used_blocks, size_type new_capacity_blocks) {
    if (new_capacity_blocks == capacity_blocks_) {
      return;
    }
    reallocate_copy_prefix(used_blocks, new_capacity_blocks);
  }

  void shrink_to_fit(size_type used_blocks) {
    if (used_blocks == capacity_blocks_) {
      return;
    }
    reallocate_copy_prefix(used_blocks, used_blocks);
  }

  block_storage clone_prefix(size_type used_blocks) const {
    block_storage copy(used_blocks, initialization::no_init);
    if (used_blocks > 0) {
      std::copy_n(data_.get(), used_blocks, copy.data_.get());
    }
    return copy;
  }

  static block_storage zeroed(size_type capacity_blocks) {
    return block_storage(capacity_blocks, initialization::zero_init);
  }

  static block_storage uninitialized(size_type capacity_blocks) {
    return block_storage(capacity_blocks, initialization::no_init);
  }

 private:
  // NOLINTBEGIN(cppcoreguidelines-avoid-c-arrays)
  static std::unique_ptr<value_type[]>
  allocate(size_type n, initialization init) {
    if (n == 0) {
      return {};
    }

    if (init == initialization::zero_init) {
      return std::unique_ptr<value_type[]>(new value_type[n]());
    }

    return std::unique_ptr<value_type[]>(new value_type[n]);
  }

  void
  reallocate_copy_prefix(size_type used_blocks, size_type new_capacity_blocks) {
    auto new_data = allocate(new_capacity_blocks, initialization::no_init);

    if (used_blocks > 0) {
      std::copy_n(data_.get(), used_blocks, new_data.get());
    }

    data_.swap(new_data);
    capacity_blocks_ = new_capacity_blocks;
  }

  std::unique_ptr<value_type[]> data_;
  size_type capacity_blocks_{0};
  // NOLINTEND(cppcoreguidelines-avoid-c-arrays)
};

} // namespace dwarfs::internal::detail
