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

#include <bit>
#include <cassert>
#include <cstddef>
#include <limits>
#include <ostream>
#include <type_traits>
#include <utility>

#include <dwarfs/internal/detail/packed_vector_heap_storage.h>
#include <dwarfs/internal/detail/packed_vector_helpers.h>
#include <dwarfs/internal/detail/packed_vector_layout.h>

namespace dwarfs::internal::detail {

template <typename Policy, typename Underlying>
class packed_vector_layout_impl<Policy, Underlying,
                                packed_vector_policy_type::heap_only> {
 public:
  using policy_type = Policy;
  using underlying_type = Underlying;
  using size_type = std::size_t;
  using storage_type = packed_vector_heap_storage<underlying_type>;

  static constexpr bool supports_inline = policy_type::supports_inline;
  static constexpr size_type bits_per_block =
      std::numeric_limits<underlying_type>::digits;
  static constexpr size_type capacity_granularity_bytes =
      policy_type::capacity_granularity_bytes;
  static constexpr size_type capacity_granularity_blocks =
      ceil_div(capacity_granularity_bytes, sizeof(underlying_type));
  static constexpr size_type max_capacity_blocks_value =
      std::numeric_limits<size_type>::max();
  static constexpr size_type max_heap_size =
      std::numeric_limits<size_type>::max();

  static void dump(std::ostream& os) {
    os << "heap-only layout\n";
    os << "  bits per block: " << bits_per_block << '\n';
    os << "  capacity granularity: " << capacity_granularity_bytes << " bytes ("
       << capacity_granularity_blocks << " blocks)\n";
    os << "  max heap size: " << max_heap_size << '\n';
    os << "  max capacity blocks: " << max_capacity_blocks_value << '\n';
  }

  static_assert(std::has_single_bit(capacity_granularity_bytes));

  [[nodiscard]] static constexpr auto
  can_store_inline(size_type, size_type) noexcept -> bool {
    return false;
  }

  [[nodiscard]] static constexpr auto
  can_store_heap(size_type bits, size_type, size_type) noexcept -> bool {
    return bits <= bits_per_block;
  }

  [[nodiscard]] auto is_inline() const noexcept -> bool { return false; }

  [[nodiscard]] auto owns_heap_storage() const noexcept -> bool {
    return data_ != nullptr;
  }

  [[nodiscard]] auto size() const noexcept -> size_type {
    return storage_type::size(data_);
  }

  [[nodiscard]] auto bits() const noexcept -> size_type { return bits_; }

  [[nodiscard]] auto capacity_blocks() const noexcept -> size_type {
    return storage_type::capacity_blocks(data_);
  }

  [[nodiscard]] auto
  usable_capacity(size_type bits) const noexcept -> size_type {
    if (data_ == nullptr) {
      return 0;
    }
    if (bits == 0) {
      return max_heap_size;
    }
    return (capacity_blocks() * bits_per_block) / bits;
  }

  void set_size(size_type v) noexcept {
    assert(data_ || v == 0);
    storage_type::set_size(data_, v);
  }

  void set_heap_state(underlying_type* data, size_type bits) noexcept {
    data_ = data;
    bits_ = bits;
  }

  void reset_empty() noexcept {
    data_ = nullptr;
    bits_ = 0;
  }

  void swap(packed_vector_layout_impl& other) noexcept {
    using std::swap;
    swap(bits_, other.bits_);
    swap(data_, other.data_);
  }

  [[nodiscard]] auto heap_data() const noexcept -> underlying_type const* {
    return data_;
  }

  [[nodiscard]] auto release_heap_data() noexcept -> underlying_type* {
    return std::exchange(data_, nullptr);
  }

  template <typename V>
  [[nodiscard]] auto read(size_type i) const -> V {
    if (bits_ == 0) {
      return V{};
    }
    return const_bit_view(data_).template read<V>({i * bits_, bits_});
  }

  template <typename V>
  void write(size_type i, V value) {
    if (bits_ == 0) {
      return;
    }
    bit_view(data_).write({i * bits_, bits_}, value);
  }

  template <typename V>
  void fill(size_type first, size_type last, V value) {
    if (bits_ == 0) {
      return;
    }

    auto view = bit_view(data_);
    for (size_type i = first; i < last; ++i) {
      view.write({i * bits_, bits_}, value);
    }
  }

 private:
  size_type bits_{0};
  underlying_type* data_{nullptr};
};

} // namespace dwarfs::internal::detail
