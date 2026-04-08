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
#include <limits>
#include <ostream>
#include <type_traits>

#include <dwarfs/bit_view.h>

#include <dwarfs/internal/detail/packed_vector_helpers.h>
#include <dwarfs/internal/detail/packed_vector_layout.h>

namespace dwarfs::internal::detail {

template <typename Policy, typename Underlying>
class packed_vector_layout_impl<Policy, Underlying,
                                packed_vector_policy_type::inline_with_heap> {
 public:
  using policy_type = compact_packed_vector_policy;
  using underlying_type = Underlying;
  using size_type = std::size_t;

  static constexpr bool supports_inline = policy_type::supports_inline;
  static constexpr size_type bits_per_block =
      std::numeric_limits<underlying_type>::digits;

  static constexpr size_type metadata_bytes = sizeof(std::size_t);
  static constexpr size_type pointer_bytes = sizeof(underlying_type*);
  static constexpr size_type state_bytes = metadata_bytes + pointer_bytes;
  static constexpr size_type state_bits = state_bytes * 8;

  static constexpr size_type inline_bits_field_bits =
      bit_width_for_max(bits_per_block);

  static constexpr size_type inline_flag_bit = 0;
  static constexpr size_type inline_bits_bit = inline_flag_bit + 1;
  static constexpr size_type inline_size_bit =
      inline_bits_bit + inline_bits_field_bits;
  static constexpr size_type inline_size_field_bits =
      policy_type::inline_size_field_bits;
  static constexpr size_type inline_header_bits =
      inline_size_bit + inline_size_field_bits;
  static constexpr size_type inline_payload_bit = inline_header_bits;
  static constexpr size_type inline_payload_bits =
      state_bits - inline_payload_bit;
  static constexpr size_type max_inline_size =
      max_for_bits<size_type>(inline_size_field_bits);

  // We can encode zero bits through capacity == 0 in heap mode and thus shave
  // one bit here since we only need to encode max_bit_width - 1.
  static constexpr size_type heap_bits_field_bits = inline_bits_field_bits - 1;

  static constexpr size_type heap_flag_bit = 0;
  static constexpr size_type heap_bits_bit = heap_flag_bit + 1;
  static constexpr size_type heap_size_bit =
      heap_bits_bit + heap_bits_field_bits;
  static constexpr size_type heap_metadata_bits = metadata_bytes * 8;
  static constexpr size_type heap_payload_field_bits =
      heap_metadata_bits - heap_size_bit;

  static constexpr size_type capacity_granularity_bytes =
      policy_type::capacity_granularity_bytes;
  static constexpr size_type capacity_granularity_blocks = std::max<size_type>(
      capacity_granularity_bytes / sizeof(underlying_type), 1);

  static_assert(std::has_single_bit(capacity_granularity_bytes));
  static_assert(std::has_single_bit(capacity_granularity_blocks));

  static void dump(std::ostream& os) {
    os << "compact layout\n";
    os << "  bits per block: " << bits_per_block << '\n';
    os << "  capacity granularity: " << capacity_granularity_bytes << " bytes ("
       << capacity_granularity_blocks << " blocks)\n";
    os << "  max inline size: " << max_inline_size << '\n';
    os << "  max heap size: " << max_heap_size << '\n';
    os << "  max capacity blocks: " << max_capacity_blocks_value << '\n';
    os << "  inline flag bit: " << inline_flag_bit << " (1 bit)\n";
    os << "  inline bits field: " << inline_bits_bit << " ("
       << inline_bits_field_bits << " bits)\n";
    os << "  inline size field: " << inline_size_bit << " ("
       << inline_size_field_bits << " bits)\n";
    os << "  inline payload bits: " << inline_payload_bits << '\n';
    os << "  heap flag bit: " << heap_flag_bit << " (1 bit)\n";
    os << "  heap bits field: " << heap_bits_bit << " (" << heap_bits_field_bits
       << " bits)\n";
    os << "  heap size field: " << heap_size_bit << " (" << heap_size_field_bits
       << " bits)\n";
    os << "  heap capacity field: " << heap_capacity_bit << " ("
       << heap_capacity_field_bits << " bits)\n";
  }

  struct heap_repr {
    std::array<std::byte, metadata_bytes> meta{};
    underlying_type* data{nullptr};
  };

  using state_type = std::array<std::byte, state_bytes>;

  static_assert(std::has_single_bit(capacity_granularity_bytes));
  static_assert(std::is_trivially_copyable_v<heap_repr>);
  static_assert(std::is_trivially_copyable_v<state_type>);
  static_assert(sizeof(heap_repr) == sizeof(state_type));
  static_assert(sizeof(state_type) % sizeof(underlying_type) == 0);
  static_assert(inline_payload_bits > 0);
  static_assert(capacity_granularity_blocks > 0);
  static_assert(heap_payload_field_bits > 0);

  static consteval auto derive_heap_size_field_bits() noexcept -> size_type {
    static constexpr auto R = heap_payload_field_bits;
    static constexpr size_type S =
        std::countr_zero(capacity_granularity_blocks);

    if constexpr (S == 0) {
      return R / 2;
    } else {
      return std::min<size_type>(R - 1, (R + S - 1) / 2);
    }
  }

  static constexpr size_type heap_size_field_bits =
      derive_heap_size_field_bits();
  static constexpr size_type heap_capacity_field_bits =
      heap_payload_field_bits - heap_size_field_bits;
  static constexpr size_type heap_capacity_bit =
      heap_size_bit + heap_size_field_bits;

  static constexpr size_type max_heap_size =
      max_for_bits<size_type>(heap_size_field_bits);
  static constexpr size_type max_heap_capacity_quanta =
      max_for_bits<size_type>(heap_capacity_field_bits);
  static constexpr size_type max_capacity_blocks_value =
      saturating_mul(max_heap_capacity_quanta, capacity_granularity_blocks);

  [[nodiscard]] static constexpr auto
  can_store_inline(size_type bits, size_type size) noexcept -> bool {
    if (bits > bits_per_block || size > max_inline_size) {
      return false;
    }
    return bits == 0 || size * bits <= inline_payload_bits;
  }

  [[nodiscard]] static constexpr auto
  can_store_heap(size_type bits, size_type size,
                 size_type capacity_blocks) noexcept -> bool {
    return bits <= bits_per_block && size <= max_heap_size &&
           capacity_blocks <= max_capacity_blocks_value &&
           (capacity_blocks == 0 ||
            capacity_blocks % capacity_granularity_blocks == 0);
  }

  packed_vector_layout_impl() { reset_empty(); }

  [[nodiscard]] auto is_inline() const noexcept -> bool {
    return read_field(inline_flag_bit, 1) != 0;
  }

  [[nodiscard]] auto owns_heap_storage() const noexcept -> bool {
    return !is_inline() && heap_data() != nullptr;
  }

  [[nodiscard]] auto size() const noexcept -> size_type {
    return is_inline() ? read_field(inline_size_bit, inline_size_field_bits)
                       : read_field(heap_size_bit, heap_size_field_bits);
  }

  [[nodiscard]] auto bits() const noexcept -> size_type {
    if (is_inline()) {
      return read_field(inline_bits_bit, inline_bits_field_bits);
    }
    if (capacity_blocks() == 0) {
      return 0;
    }
    return read_heap_bits();
  }

  [[nodiscard]] auto capacity_blocks() const noexcept -> size_type {
    if (is_inline()) {
      return 0;
    }
    return read_field(heap_capacity_bit, heap_capacity_field_bits) *
           capacity_granularity_blocks;
  }

  [[nodiscard]] static auto
  inline_capacity_for_bits(size_type bits) noexcept -> size_type {
    if (bits == 0) {
      return max_inline_size;
    }
    return std::min(max_inline_size, inline_payload_bits / bits);
  }

  [[nodiscard]] auto
  usable_capacity(size_type bits) const noexcept -> size_type {
    if (is_inline()) {
      return inline_capacity_for_bits(bits);
    }
    if (bits == 0) {
      return max_heap_size;
    }
    return std::min(max_heap_size, (capacity_blocks() * bits_per_block) / bits);
  }

  void set_size(size_type v) noexcept {
    if (is_inline()) {
      write_field(inline_size_bit, inline_size_field_bits, v);
    } else {
      write_field(heap_size_bit, heap_size_field_bits, v);
    }
  }

  void set_bits(size_type v) noexcept {
    if (is_inline()) {
      write_field(inline_bits_bit, inline_bits_field_bits, v);
    } else {
      write_heap_bits(v);
    }
  }

  void set_capacity_blocks(size_type v) noexcept {
    if (is_inline()) {
      return;
    }

    assert(v == 0 || v % capacity_granularity_blocks == 0);
    write_field(heap_capacity_bit, heap_capacity_field_bits,
                v / capacity_granularity_blocks);
  }

  void set_inline_state(size_type bits, size_type size) noexcept {
    assert(can_store_inline(bits, size));

    state_.fill(std::byte{0});
    write_field(inline_flag_bit, 1, 1);
    write_field(inline_bits_bit, inline_bits_field_bits, bits);
    write_field(inline_size_bit, inline_size_field_bits, size);
  }

  void set_heap_state(underlying_type* data, size_type bits, size_type size,
                      size_type capacity_blocks) noexcept {
    assert(can_store_heap(bits, size, capacity_blocks));

    state_.fill(std::byte{0});
    write_field(heap_flag_bit, 1, 0);
    write_heap_bits(bits);
    write_field(heap_size_bit, heap_size_field_bits, size);
    write_field(heap_capacity_bit, heap_capacity_field_bits,
                capacity_blocks / capacity_granularity_blocks);
    set_heap_data(data);
  }

  void reset_empty() noexcept { set_inline_state(0, 0); }

  void swap(packed_vector_layout_impl& other) noexcept {
    using std::swap;
    swap(state_, other.state_);
  }

  [[nodiscard]] auto heap_data() const noexcept -> underlying_type const* {
    assert(!is_inline());
    return std::bit_cast<heap_repr>(state_).data;
  }

  [[nodiscard]] auto release_heap_data() noexcept -> underlying_type* {
    if (is_inline()) {
      return nullptr;
    }

    auto* p = const_cast<underlying_type*>(heap_data());
    set_heap_data(nullptr);
    return p;
  }

  template <typename V>
  [[nodiscard]] auto read(size_type i) const -> V {
    auto const value_bits = bits();
    if (value_bits == 0) {
      return V{};
    }

    if (is_inline()) {
      return const_bit_view(state_.data())
          .template read<V>({inline_payload_bit + i * value_bits, value_bits});
    }

    return const_bit_view(heap_data())
        .template read<V>({i * value_bits, value_bits});
  }

  template <typename V>
  void write(size_type i, V value) {
    auto const value_bits = bits();
    if (value_bits == 0) {
      return;
    }

    if (is_inline()) {
      bit_view(state_.data())
          .write({inline_payload_bit + i * value_bits, value_bits}, value);
      return;
    }

    bit_view(const_cast<underlying_type*>(heap_data()))
        .write({i * value_bits, value_bits}, value);
  }

  template <typename V>
  void fill(size_type first, size_type last, V value) {
    auto const value_bits = bits();
    if (value_bits == 0) {
      return;
    }

    if (is_inline()) {
      auto view = bit_view(state_.data());
      for (size_type i = first; i < last; ++i) {
        view.write({inline_payload_bit + i * value_bits, value_bits}, value);
      }
      return;
    }

    auto view = bit_view(const_cast<underlying_type*>(heap_data()));
    for (size_type i = first; i < last; ++i) {
      view.write({i * value_bits, value_bits}, value);
    }
  }

 private:
  [[nodiscard]] auto read_heap_bits() const noexcept -> size_type {
    return read_field(heap_bits_bit, heap_bits_field_bits) + 1;
  }

  void write_heap_bits(size_type v) noexcept {
    assert(!is_inline());
    write_field(heap_bits_bit, heap_bits_field_bits,
                std::max<size_type>(v, 1) - 1);
  }

  [[nodiscard]] auto
  read_field(size_type offset, size_type width) const noexcept -> size_type {
    assert(width > 0);
    return const_bit_view(state_.data())
        .template read<size_type>({offset, width});
  }

  void
  write_field(size_type offset, size_type width, size_type value) noexcept {
    assert(width > 0);
    bit_view(state_.data()).write({offset, width}, value);
  }

  void set_heap_data(underlying_type* p) noexcept {
    auto repr = std::bit_cast<heap_repr>(state_);
    repr.data = p;
    state_ = std::bit_cast<state_type>(repr);
  }

  state_type state_{};
};

} // namespace dwarfs::internal::detail
