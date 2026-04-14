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
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ostream>
#include <type_traits>
#include <utility>

#include <dwarfs/bit_view.h>

#include <dwarfs/container/detail/packed_field_descriptor.h>
#include <dwarfs/container/detail/packed_vector_heap_storage.h>
#include <dwarfs/container/detail/packed_vector_helpers.h>
#include <dwarfs/container/detail/packed_vector_layout.h>

namespace dwarfs::container::detail {

template <typename Policy, typename Value, typename Underlying>
class packed_vector_layout_impl<Policy, Value, Underlying,
                                packed_vector_policy_type::heap_only> {
 public:
  using policy_type = Policy;
  using value_type = Value;
  using underlying_type = Underlying;
  using size_type = std::size_t;
  using storage_type = packed_vector_heap_storage<underlying_type>;
  using field_descriptor = packed_field_descriptor<value_type>;
  using widths_type = typename field_descriptor::widths_type;

  static constexpr bool supports_inline = policy_type::supports_inline;
  static constexpr size_type field_count = field_descriptor::field_count;
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

  template <size_type I>
  using field_encoded_type =
      typename field_descriptor::template field_encoded_type<I>;

  static void dump(std::ostream& os) {
    os << "heap-only layout\n";
    os << "  field count: " << field_count << '\n';
    os << "  bits per block: " << bits_per_block << '\n';
    os << "  capacity granularity: " << capacity_granularity_bytes << " bytes ("
       << capacity_granularity_blocks << " blocks)\n";
    os << "  max heap size: " << max_heap_size << '\n';
    os << "  max capacity blocks: " << max_capacity_blocks_value << '\n';
  }

  static_assert(std::has_single_bit(capacity_granularity_bytes));

  [[nodiscard]] static constexpr auto
  can_store_inline(widths_type const&, size_type) noexcept -> bool {
    return false;
  }

  [[nodiscard]] static constexpr auto
  can_store_heap(widths_type const& widths, size_type, size_type) noexcept
      -> bool {
    if constexpr (!std::same_as<underlying_type, std::uint8_t>) {
      for (auto const bits : widths) {
        if (bits > bits_per_block) {
          return false;
        }
      }
    }
    return true;
  }

  packed_vector_layout_impl() { reset_empty(); }

  [[nodiscard]] auto is_inline() const noexcept -> bool { return false; }

  [[nodiscard]] auto owns_heap_storage() const noexcept -> bool {
    return data_ != nullptr;
  }

  [[nodiscard]] auto size() const noexcept -> size_type {
    return storage_type::size(data_);
  }

  [[nodiscard]] auto widths() const noexcept -> widths_type const& {
    return widths_;
  }

  template <size_type I>
  [[nodiscard]] auto field_bits() const noexcept -> size_type {
    static_assert(I < field_count);
    return widths_[I];
  }

  [[nodiscard]] auto total_bits() const noexcept -> size_type {
    return total_bits_for(widths_);
  }

  [[nodiscard]] auto capacity_blocks() const noexcept -> size_type {
    return storage_type::capacity_blocks(data_);
  }

  [[nodiscard]] auto
  usable_capacity(widths_type const& widths) const noexcept -> size_type {
    if (data_ == nullptr) {
      return 0;
    }

    auto const stride_bits = total_bits_for(widths);
    if (stride_bits == 0) {
      return max_heap_size;
    }

    return (capacity_blocks() * bits_per_block) / stride_bits;
  }

  void set_size(size_type v) noexcept {
    assert(data_ || v == 0);
    storage_type::set_size(data_, v);
  }

  void set_widths(widths_type const& widths) noexcept { widths_ = widths; }

  void
  set_heap_state(underlying_type* data, widths_type const& widths) noexcept {
    data_ = data;
    widths_ = widths;
  }

  void reset_empty() noexcept {
    data_ = nullptr;
    widths_.fill(0);
  }

  void swap(packed_vector_layout_impl& other) noexcept {
    using std::swap;
    swap(widths_, other.widths_);
    swap(data_, other.data_);
  }

  [[nodiscard]] auto heap_data() const noexcept -> underlying_type const* {
    return data_;
  }

  [[nodiscard]] auto release_heap_data() noexcept -> underlying_type* {
    return std::exchange(data_, nullptr);
  }

  template <size_type I>
  [[nodiscard]] auto
  read_field(size_type index) const -> field_encoded_type<I> {
    static_assert(I < field_count);

    if (field_bits<I>() == 0) {
      return field_encoded_type<I>{};
    }

    return const_bit_view(data_).template read<field_encoded_type<I>>(
        {field_offset_bits<I>(index), field_bits<I>()});
  }

  template <size_type I>
  void write_field(size_type index, field_encoded_type<I> value) {
    static_assert(I < field_count);

    if (field_bits<I>() == 0) {
      return;
    }

    bit_view(data_).write({field_offset_bits<I>(index), field_bits<I>()},
                          value);
  }

  template <size_type I>
  void
  fill_field(size_type first, size_type last, field_encoded_type<I> value) {
    static_assert(I < field_count);

    if (field_bits<I>() == 0) {
      return;
    }

    auto view = bit_view(data_);
    for (size_type i = first; i < last; ++i) {
      view.write({field_offset_bits<I>(i), field_bits<I>()}, value);
    }
  }

  // Scalar compatibility shims. These can go away once the vector no longer
  // uses the scalar-shaped layout API.
  [[nodiscard]] static constexpr auto
  can_store_inline(size_type bits, size_type size) noexcept -> bool
    requires(field_count == 1)
  {
    return can_store_inline(widths_type{static_cast<std::uint8_t>(bits)}, size);
  }

  [[nodiscard]] static constexpr auto
  can_store_heap(size_type bits, size_type size,
                 size_type capacity_blocks) noexcept -> bool
    requires(field_count == 1)
  {
    return can_store_heap(widths_type{static_cast<std::uint8_t>(bits)}, size,
                          capacity_blocks);
  }

  [[nodiscard]] auto bits() const noexcept -> size_type
    requires(field_count == 1)
  {
    return widths_[0];
  }

  [[nodiscard]] auto usable_capacity(size_type bits) const noexcept -> size_type
    requires(field_count == 1)
  {
    return usable_capacity(widths_type{static_cast<std::uint8_t>(bits)});
  }

  void set_heap_state(underlying_type* data, size_type bits) noexcept
    requires(field_count == 1)
  {
    set_heap_state(data, widths_type{static_cast<std::uint8_t>(bits)});
  }

  template <typename V>
  [[nodiscard]] auto read(size_type i) const -> V
    requires(field_count == 1)
  {
    return static_cast<V>(read_field<0>(i));
  }

  template <typename V>
  void write(size_type i, V value)
    requires(field_count == 1)
  {
    write_field<0>(i, static_cast<field_encoded_type<0>>(value));
  }

  template <typename V>
  void fill(size_type first, size_type last, V value)
    requires(field_count == 1)
  {
    fill_field<0>(first, last, static_cast<field_encoded_type<0>>(value));
  }

 private:
  [[nodiscard]] static constexpr auto
  total_bits_for(widths_type const& widths) noexcept -> size_type {
    size_type total = 0;
    for (auto const bits : widths) {
      total += bits;
    }
    return total;
  }

  template <size_type I>
  [[nodiscard]] auto field_prefix_bits() const noexcept -> size_type {
    static_assert(I < field_count);

    size_type prefix = 0;
    for (size_type j = 0; j < I; ++j) {
      prefix += widths_[j];
    }
    return prefix;
  }

  template <size_type I>
  [[nodiscard]] auto
  field_offset_bits(size_type index) const noexcept -> size_type {
    return index * total_bits() + field_prefix_bits<I>();
  }

  widths_type widths_{};
  underlying_type* data_{nullptr};
};

} // namespace dwarfs::container::detail
