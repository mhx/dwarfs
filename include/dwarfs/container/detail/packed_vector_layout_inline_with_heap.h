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
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
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
                                packed_vector_policy_type::inline_with_heap> {
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

  template <size_type I>
  using field_encoded_type =
      typename field_descriptor::template field_encoded_type<I>;

 private:
  template <size_type I>
  static constexpr auto max_field_bits() noexcept -> size_type {
    using encoded_type_i = field_encoded_type<I>;
    using unsigned_encoded_type_i = std::make_unsigned_t<encoded_type_i>;
    return std::numeric_limits<unsigned_encoded_type_i>::digits;
  }

  template <size_type... I>
  static constexpr auto
  max_widths_impl(std::index_sequence<I...>) noexcept -> widths_type {
    return widths_type{static_cast<std::uint8_t>(max_field_bits<I>())...};
  }

  template <size_type... I>
  static constexpr auto
  max_storable_width_impl(std::index_sequence<I...>) noexcept -> size_type {
    size_type result = 0;
    ((result = std::max(result, max_field_bits<I>())), ...);
    return result;
  }

  [[nodiscard]] static constexpr auto
  max_widths_value() noexcept -> widths_type {
    return max_widths_impl(std::make_index_sequence<field_count>{});
  }

  [[nodiscard]] static constexpr auto
  max_storable_width() noexcept -> size_type {
    return max_storable_width_impl(std::make_index_sequence<field_count>{});
  }

  [[nodiscard]] static constexpr auto zero_widths() noexcept -> widths_type {
    widths_type widths{};
    widths.fill(0);
    return widths;
  }

  [[nodiscard]] static constexpr auto
  total_bits_for(widths_type const& widths) noexcept -> size_type {
    size_type total = 0;
    for (auto const bits : widths) {
      total += bits;
    }
    return total;
  }

  [[nodiscard]] static constexpr auto
  widths_fit(widths_type const& widths) noexcept -> bool {
    auto const max = max_widths_value();
    for (size_type i = 0; i < field_count; ++i) {
      if (widths[i] > max[i]) {
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] static constexpr auto
  width_field_offset(size_type i) noexcept -> size_type {
    return 1 + i * width_field_bits;
  }

  [[nodiscard]] auto read_width(size_type i) const noexcept -> std::uint8_t {
    assert(i < field_count);
    return static_cast<std::uint8_t>(
        const_bit_view(state_.data())
            .template read<size_type>(
                {width_field_offset(i), width_field_bits}));
  }

  void write_width(size_type i, std::uint8_t bits) noexcept {
    assert(i < field_count);
    bit_view(state_.data())
        .write({width_field_offset(i), width_field_bits},
               static_cast<size_type>(bits));
  }

  [[nodiscard]] auto read_widths() const noexcept -> widths_type {
    widths_type widths{};
    for (size_type i = 0; i < field_count; ++i) {
      widths[i] = read_width(i);
    }
    return widths;
  }

  void write_widths(widths_type const& widths) noexcept {
    for (size_type i = 0; i < field_count; ++i) {
      write_width(i, widths[i]);
    }
  }

  template <size_type I>
  [[nodiscard]] auto field_prefix_bits() const noexcept -> size_type {
    static_assert(I < field_count);
    size_type prefix = 0;
    for (size_type j = 0; j < I; ++j) {
      prefix += read_width(j);
    }
    return prefix;
  }

  [[nodiscard]] auto total_bits() const noexcept -> size_type {
    return total_bits_for(read_widths());
  }

  template <size_type I>
  [[nodiscard]] auto
  field_offset_bits(size_type index) const noexcept -> size_type {
    return index * total_bits() + field_prefix_bits<I>();
  }

  template <size_type I>
  [[nodiscard]] auto
  inline_field_offset_bits(size_type index) const noexcept -> size_type {
    return inline_payload_bit + field_offset_bits<I>(index);
  }

 public:
  static constexpr size_type inline_size_field_bits =
      policy_type::inline_size_field_bits;
  static constexpr size_type max_inline_size =
      max_for_bits<size_type>(inline_size_field_bits);

  static constexpr size_type capacity_granularity_bytes =
      policy_type::capacity_granularity_bytes;
  static constexpr size_type capacity_granularity_blocks = std::max<size_type>(
      capacity_granularity_bytes / sizeof(underlying_type), 1);

  static constexpr size_type max_heap_size =
      std::numeric_limits<size_type>::max();
  static constexpr size_type max_capacity_blocks_value =
      std::numeric_limits<size_type>::max();

  static constexpr size_type pointer_bytes = sizeof(underlying_type*);
  static constexpr size_type pointer_bits = pointer_bytes * 8;

  static constexpr size_type width_field_bits =
      bit_width_for_max(max_storable_width());

  static constexpr size_type widths_bits = field_count * width_field_bits;

  static constexpr size_type inline_flag_bit = 0;
  static constexpr size_type inline_size_bit = 1 + widths_bits;
  static constexpr size_type inline_header_bits =
      inline_size_bit + inline_size_field_bits;
  static constexpr size_type inline_payload_bit = inline_header_bits;

  static constexpr size_type heap_flag_bit = 0;
  static constexpr size_type heap_header_bits = 1 + widths_bits;

  static constexpr size_type heap_metadata_bytes =
      ceil_div(heap_header_bits, size_type{8});

  static constexpr size_type min_heap_state_bits =
      (heap_metadata_bytes + pointer_bytes) * 8;
  static constexpr size_type min_inline_state_bits =
      inline_header_bits + pointer_bits;

  static constexpr size_type state_bits = round_up_to_multiple(
      std::max(min_heap_state_bits, min_inline_state_bits), size_type{64});
  static constexpr size_type state_bytes = state_bits / 8;

  static constexpr size_type heap_pointer_offset_bytes =
      state_bytes - pointer_bytes;
  static constexpr size_type heap_pointer_bit = heap_pointer_offset_bytes * 8;

  static constexpr size_type inline_payload_bits =
      state_bits - inline_payload_bit;

  using state_type = std::array<std::byte, state_bytes>;

  static_assert(std::has_single_bit(capacity_granularity_bytes));
  static_assert(std::is_trivially_copyable_v<state_type>);
  static_assert(sizeof(state_type) % sizeof(underlying_type) == 0);
  static_assert(capacity_granularity_blocks > 0);
  static_assert(heap_header_bits <= heap_pointer_bit);
  static_assert(inline_payload_bits >= pointer_bits);

  static void dump(std::ostream& os) {
    os << "compact layout\n";
    os << "  field count: " << field_count << '\n';
    os << "  bits per block: " << bits_per_block << '\n';
    os << "  capacity granularity: " << capacity_granularity_bytes << " bytes ("
       << capacity_granularity_blocks << " blocks)\n";
    os << "  state bytes: " << state_bytes << '\n';
    os << "  heap pointer offset: " << heap_pointer_offset_bytes << " bytes\n";
    os << "  width field bits: " << width_field_bits << '\n';
    os << "  inline header bits: " << inline_header_bits << '\n';
    os << "  inline payload bits: " << inline_payload_bits << '\n';
    os << "  max inline size: " << max_inline_size << '\n';
    os << "  max heap size: " << max_heap_size << '\n';
    os << "  max capacity blocks: " << max_capacity_blocks_value << '\n';
  }

  packed_vector_layout_impl() { reset_empty(); }

  [[nodiscard]] auto is_inline() const noexcept -> bool {
    return read_field_bit(inline_flag_bit);
  }

  [[nodiscard]] auto owns_heap_storage() const noexcept -> bool {
    return !is_inline() && heap_data() != nullptr;
  }

  [[nodiscard]] auto mutable_heap_data() const noexcept -> underlying_type* {
    assert(!is_inline());
    return const_cast<underlying_type*>(heap_data());
  }

  [[nodiscard]] auto size() const noexcept -> size_type {
    if (is_inline()) {
      return const_bit_view(state_.data())
          .template read<size_type>({inline_size_bit, inline_size_field_bits});
    }
    return storage_type::size(mutable_heap_data());
  }

  [[nodiscard]] auto widths() const noexcept -> widths_type {
    return read_widths();
  }

  template <size_type I>
  [[nodiscard]] auto field_bits() const noexcept -> size_type {
    static_assert(I < field_count);
    return read_width(I);
  }

  [[nodiscard]] auto capacity_blocks() const noexcept -> size_type {
    if (is_inline()) {
      return 0;
    }
    return storage_type::capacity_blocks(mutable_heap_data());
  }

  [[nodiscard]] static constexpr auto
  can_store_inline(widths_type const& widths, size_type size) noexcept -> bool {
    if (!widths_fit(widths) || size > max_inline_size) {
      return false;
    }

    auto const stride_bits = total_bits_for(widths);
    if (stride_bits == 0) {
      return true;
    }

    return size <= inline_payload_bits / stride_bits;
  }

  [[nodiscard]] static constexpr auto
  can_store_heap(widths_type const& widths, size_type, size_type) noexcept
      -> bool {
    if (!widths_fit(widths)) {
      return false;
    }

    if constexpr (std::same_as<underlying_type, std::uint8_t>) {
      return true;
    } else {
      return std::ranges::all_of(
          widths, [](auto bits) { return bits <= bits_per_block; });
    }
  }

  [[nodiscard]] static constexpr auto
  inline_capacity_for_widths(widths_type const& widths) noexcept -> size_type {
    if (!widths_fit(widths)) {
      return 0;
    }

    auto const stride_bits = total_bits_for(widths);
    if (stride_bits == 0) {
      return max_inline_size;
    }

    return std::min(max_inline_size, inline_payload_bits / stride_bits);
  }

  [[nodiscard]] auto
  usable_capacity(widths_type const& widths) const noexcept -> size_type {
    if (is_inline()) {
      return inline_capacity_for_widths(widths);
    }

    assert(heap_data() != nullptr);

    auto const stride_bits = total_bits_for(widths);
    if (stride_bits == 0) {
      return max_heap_size;
    }

    return (capacity_blocks() * bits_per_block) / stride_bits;
  }

  void set_size(size_type v) noexcept {
    if (is_inline()) {
      bit_view(state_.data())
          .write({inline_size_bit, inline_size_field_bits}, v);
    } else {
      storage_type::set_size(mutable_heap_data(), v);
    }
  }

  void set_inline_state(widths_type const& widths, size_type size) noexcept {
    assert(can_store_inline(widths, size));

    state_.fill(std::byte{0});
    write_field_bit(inline_flag_bit, true);
    write_widths(widths);
    bit_view(state_.data())
        .write({inline_size_bit, inline_size_field_bits}, size);
  }

  void
  set_heap_state(underlying_type* data, widths_type const& widths) noexcept {
    assert(can_store_heap(widths, 0, 0));

    state_.fill(std::byte{0});
    write_field_bit(heap_flag_bit, false);
    write_widths(widths);
    set_heap_data(data);
  }

  void reset_empty() noexcept { set_inline_state(zero_widths(), 0); }

  void swap(packed_vector_layout_impl& other) noexcept {
    using std::swap;
    swap(state_, other.state_);
  }

  [[nodiscard]] auto heap_data() const noexcept -> underlying_type const* {
    assert(!is_inline());
    underlying_type* p = nullptr;
    std::memcpy(&p, state_.data() + heap_pointer_offset_bytes, sizeof(p));
    return p;
  }

  [[nodiscard]] auto release_heap_data() noexcept -> underlying_type* {
    if (is_inline()) {
      return nullptr;
    }

    auto* p = mutable_heap_data();
    set_heap_data(nullptr);
    return p;
  }

  template <size_type I>
  [[nodiscard]] auto
  read_field(size_type index) const -> field_encoded_type<I> {
    static_assert(I < field_count);

    auto const bits = field_bits<I>();
    if (bits == 0) {
      return field_encoded_type<I>{};
    }

    if (is_inline()) {
      return const_bit_view(state_.data())
          .template read<field_encoded_type<I>>(
              {inline_field_offset_bits<I>(index), bits});
    }

    return const_bit_view(heap_data())
        .template read<field_encoded_type<I>>(
            {field_offset_bits<I>(index), bits});
  }

  template <size_type I>
  void write_field(size_type index, field_encoded_type<I> value) {
    static_assert(I < field_count);

    auto const bits = field_bits<I>();
    if (bits == 0) {
      return;
    }

    if (is_inline()) {
      bit_view(state_.data())
          .write({inline_field_offset_bits<I>(index), bits}, value);
      return;
    }

    bit_view(mutable_heap_data())
        .write({field_offset_bits<I>(index), bits}, value);
  }

  template <size_type I>
  void
  fill_field(size_type first, size_type last, field_encoded_type<I> value) {
    static_assert(I < field_count);

    auto const bits = field_bits<I>();
    if (bits == 0) {
      return;
    }

    if (is_inline()) {
      auto view = bit_view(state_.data());
      for (size_type i = first; i < last; ++i) {
        view.write({inline_field_offset_bits<I>(i), bits}, value);
      }
      return;
    }

    auto view = bit_view(mutable_heap_data());
    for (size_type i = first; i < last; ++i) {
      view.write({field_offset_bits<I>(i), bits}, value);
    }
  }

  // Scalar compatibility API.
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

  [[nodiscard]] static constexpr auto
  inline_capacity_for_bits(size_type bits) noexcept -> size_type
    requires(field_count == 1)
  {
    return inline_capacity_for_widths(
        widths_type{static_cast<std::uint8_t>(bits)});
  }

  [[nodiscard]] auto usable_capacity(size_type bits) const noexcept -> size_type
    requires(field_count == 1)
  {
    return usable_capacity(widths_type{static_cast<std::uint8_t>(bits)});
  }

  void set_inline_state(size_type bits, size_type size) noexcept
    requires(field_count == 1)
  {
    set_inline_state(widths_type{static_cast<std::uint8_t>(bits)}, size);
  }

  void set_heap_state(underlying_type* data, size_type bits) noexcept
    requires(field_count == 1)
  {
    set_heap_state(data, widths_type{static_cast<std::uint8_t>(bits)});
  }

  [[nodiscard]] auto bits() const noexcept -> size_type
    requires(field_count == 1)
  {
    return widths()[0];
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
  [[nodiscard]] auto read_field_bit(size_type bit) const noexcept -> bool {
    return const_bit_view(state_.data()).template read<unsigned>({bit, 1}) != 0;
  }

  void write_field_bit(size_type bit, bool value) noexcept {
    bit_view(state_.data()).write({bit, 1}, static_cast<unsigned>(value));
  }

  void set_heap_data(underlying_type* p) noexcept {
    std::memcpy(state_.data() + heap_pointer_offset_bytes, &p, sizeof(p));
  }

  state_type state_{};
};

} // namespace dwarfs::container::detail
