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
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ostream>
#include <ranges>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include <dwarfs/ranges.h>

#include <dwarfs/container/detail/index_based_iterator.h>
#include <dwarfs/container/detail/index_based_value_proxy.h>
#include <dwarfs/container/detail/packed_field_descriptor.h>
#include <dwarfs/container/detail/packed_storage_selector.h>
#include <dwarfs/container/detail/packed_vector_heap_storage.h>
#include <dwarfs/container/detail/packed_vector_helpers.h>
#include <dwarfs/container/detail/packed_vector_layout.h>
#include <dwarfs/container/detail/vector_growth_policy.h>
#include <dwarfs/container/packed_value_traits.h>

namespace dwarfs::container {

template <typename T>
concept packed_vector_value = requires {
  typename detail::packed_field_descriptor<T>;
  typename detail::packed_field_descriptor<T>::widths_type;
  requires(detail::packed_field_descriptor<T>::field_count > 0);
};

enum class packed_vector_bit_width_strategy {
  fixed,
  automatic,
};

/**
 * Packed vector with configurable bit-width strategy and layout.
 *
 * TODO: update the docs
 *
 * For scalar values, this behaves like the original packed integer vector.
 * For tuple values, widths are tracked per field and values are stored in an
 * interleaved packed representation.
 *
 * Stores integral values in a densely bit-packed representation using exactly
 * `bits()` bits per element. Element access is random-access, but references
 * are represented by proxy objects rather than native `T&`.
 *
 * The vector supports two bit-width strategies:
 * - fixed: the bit width is chosen explicitly and remains unchanged unless
 *   `truncate_to_bits()` is called
 * - automatic: the bit width grows as needed to represent newly assigned values
 *   and can be reduced explicitly via `optimize_storage()`
 *
 * Storage layout is controlled by the policy:
 * - heap-only policies always use heap storage for non-empty vectors
 * - compact policies may store small vectors inline and fall back to heap
 *   storage as needed
 *
 * Heap storage is shared between policy variants and stores the logical size
 * and reserved capacity together with the packed payload. Empty vectors require
 * no heap allocation.
 *
 * Iterators are random-access iterators implemented as index-based iterators.
 * They remain usable across storage relocation and repacking as long as the
 * referenced index remains valid, but operations that replace the logical
 * contents of the vector should be treated as invalidating all iterators.
 */
template <packed_vector_value T,
          packed_vector_bit_width_strategy BitWidthStrategy, typename Policy,
          typename GrowthPolicy = detail::default_block_growth_policy>
class basic_packed_int_vector {
 public:
  using value_type = T;
  using size_type = std::size_t;
  using policy_type = Policy;
  using growth_policy_type = GrowthPolicy;
  using reference = detail::index_based_value_proxy<basic_packed_int_vector>;
  using const_reference = value_type;
  using iterator = detail::index_based_iterator<basic_packed_int_vector>;
  using const_iterator =
      detail::index_based_const_iterator<basic_packed_int_vector>;

 private:
  using field_descriptor = detail::packed_field_descriptor<value_type>;
  static constexpr size_type field_count = field_descriptor::field_count;

  template <size_type I>
  using field_traits_type =
      typename field_descriptor::template field_traits_type<I>;

  template <size_type I>
  using field_encoded_type =
      typename field_descriptor::template field_encoded_type<I>;

  using storage_selector = detail::packed_storage_selector<value_type>;

 public:
  using widths_type = typename field_descriptor::widths_type;
  using underlying_type = typename storage_selector::underlying_type;

 private:
  using storage_type = detail::packed_vector_heap_storage<underlying_type>;
  using init_mode = typename storage_type::initialization;
  using layout_type =
      detail::packed_vector_layout<policy_type, value_type, underlying_type>;

  template <packed_vector_bit_width_strategy OtherStrategy,
            typename OtherPolicy, typename OtherGrowthPolicy>
  using other_vector_type =
      basic_packed_int_vector<value_type, OtherStrategy, OtherPolicy,
                              OtherGrowthPolicy>;

  template <typename OtherLayout>
  static constexpr bool same_layout_v = std::same_as<layout_type, OtherLayout>;

 public:
  static constexpr bool auto_bit_width =
      BitWidthStrategy == packed_vector_bit_width_strategy::automatic;
  static constexpr bool has_inline_storage = layout_type::supports_inline;
  static constexpr size_type bits_per_block =
      std::numeric_limits<underlying_type>::digits;

  template <packed_vector_value, packed_vector_bit_width_strategy, typename,
            typename>
  friend class basic_packed_int_vector;

  static constexpr auto max_size() noexcept -> size_type {
    return layout_type::max_heap_size;
  }

  static void dump_layout(std::ostream& os) { layout_type::dump(os); }

  static constexpr auto field_arity() noexcept -> size_type {
    return field_count;
  }

  static constexpr auto max_widths() noexcept -> widths_type {
    return max_widths_impl(std::make_index_sequence<field_count>{});
  }

  static constexpr auto
  required_widths(value_type const& value) noexcept -> widths_type {
    widths_type widths{};
    widths.fill(0);

    field_descriptor::encode_with(value, [&]<size_type I>(auto encoded) {
      widths[I] = static_cast<std::uint8_t>(required_bits_encoded<I>(encoded));
    });

    return widths;
  }

  static constexpr auto
  required_bits(value_type const& value) noexcept -> size_type
    requires(field_count == 1)
  {
    return required_widths(value)[0];
  }

  static constexpr auto
  inline_capacity_for_widths(widths_type const& widths) noexcept -> size_type
    requires has_inline_storage
  {
    return layout_type::inline_capacity_for_widths(widths);
  }

  static constexpr auto
  inline_capacity_for_bits(size_type bits) noexcept -> size_type
    requires(has_inline_storage && field_count == 1)
  {
    return inline_capacity_for_widths(make_widths(bits));
  }

  basic_packed_int_vector() = default;

  explicit basic_packed_int_vector(widths_type const& widths) {
    initialize(checked_widths(widths), 0);
  }

  basic_packed_int_vector(widths_type const& widths, size_type size) {
    check_size_limit(size);
    initialize(checked_widths(widths), size);
  }

  explicit basic_packed_int_vector(size_type bits)
    requires(field_count == 1)
      : basic_packed_int_vector(make_widths(bits)) {}

  basic_packed_int_vector(size_type bits, size_type size)
    requires(field_count == 1)
      : basic_packed_int_vector(make_widths(bits), size) {}

  basic_packed_int_vector(std::initializer_list<value_type> ilist)
      : basic_packed_int_vector(required_widths_for(ilist), ilist.size()) {
    size_type i = 0;
    for (auto const& value : ilist) {
      write_value(i++, value);
    }
  }

  template <std::ranges::forward_range R>
    requires std::convertible_to<std::ranges::range_reference_t<R>,
                                 value_type> &&
             std::ranges::sized_range<R>
  basic_packed_int_vector(from_range_t, R&& r)
      : basic_packed_int_vector(required_widths_for(r), std::ranges::size(r)) {
    size_type i = 0;
    for (auto&& v : std::forward<R>(r)) {
      write_value(i++, static_cast<value_type>(v));
    }
  }

  ~basic_packed_int_vector() { destroy_heap_storage(); }

  basic_packed_int_vector(basic_packed_int_vector const& other) {
    copy_from_impl(other);
  }

  basic_packed_int_vector(basic_packed_int_vector&& other) noexcept {
    move_from_impl(std::move(other));
  }

  basic_packed_int_vector& operator=(basic_packed_int_vector const& other) {
    if (this != &other) {
      basic_packed_int_vector tmp;
      tmp.copy_from_impl(other);
      swap(tmp);
    }
    return *this;
  }

  basic_packed_int_vector& operator=(basic_packed_int_vector&& other) noexcept {
    if (this != &other) {
      move_from_impl(std::move(other));
    }
    return *this;
  }

  template <packed_vector_bit_width_strategy OtherStrategy,
            typename OtherPolicy, typename OtherGrowthPolicy>
  basic_packed_int_vector(
      basic_packed_int_vector<value_type, OtherStrategy, OtherPolicy,
                              OtherGrowthPolicy> const& other) {
    copy_from_impl(other);
  }

  template <packed_vector_bit_width_strategy OtherStrategy,
            typename OtherPolicy, typename OtherGrowthPolicy>
  basic_packed_int_vector(
      basic_packed_int_vector<value_type, OtherStrategy, OtherPolicy,
                              OtherGrowthPolicy>&& other)
      noexcept(same_layout_v<typename basic_packed_int_vector<
                   value_type, OtherStrategy, OtherPolicy,
                   OtherGrowthPolicy>::layout_type>) {
    move_from_impl(std::move(other));
  }

  template <packed_vector_bit_width_strategy OtherStrategy,
            typename OtherPolicy, typename OtherGrowthPolicy>
  auto operator=(basic_packed_int_vector<value_type, OtherStrategy, OtherPolicy,
                                         OtherGrowthPolicy> const& other)
      -> basic_packed_int_vector& {
    basic_packed_int_vector tmp;
    tmp.copy_from_impl(other);
    swap(tmp);
    return *this;
  }

  template <packed_vector_bit_width_strategy OtherStrategy,
            typename OtherPolicy, typename OtherGrowthPolicy>
  auto operator=(basic_packed_int_vector<value_type, OtherStrategy, OtherPolicy,
                                         OtherGrowthPolicy>&& other)
      -> basic_packed_int_vector& {
    move_from_impl(std::move(other));
    return *this;
  }

  void swap(basic_packed_int_vector& other) noexcept {
    layout_.swap(other.layout_);
  }

  [[nodiscard]] iterator begin() noexcept { return iterator{this, 0}; }
  [[nodiscard]] iterator end() noexcept { return iterator{this, size()}; }

  [[nodiscard]] const_iterator begin() const noexcept {
    return const_iterator{this, 0};
  }

  [[nodiscard]] const_iterator end() const noexcept {
    return const_iterator{this, size()};
  }

  [[nodiscard]] const_iterator cbegin() const noexcept {
    return const_iterator{this, 0};
  }

  [[nodiscard]] const_iterator cend() const noexcept {
    return const_iterator{this, size()};
  }

  [[nodiscard]] auto rbegin() noexcept {
    return std::reverse_iterator<iterator>{end()};
  }

  [[nodiscard]] auto rend() noexcept {
    return std::reverse_iterator<iterator>{begin()};
  }

  [[nodiscard]] auto rbegin() const noexcept {
    return std::reverse_iterator<const_iterator>{end()};
  }

  [[nodiscard]] auto rend() const noexcept {
    return std::reverse_iterator<const_iterator>{begin()};
  }

  [[nodiscard]] auto crbegin() const noexcept {
    return std::reverse_iterator<const_iterator>{cend()};
  }

  [[nodiscard]] auto crend() const noexcept {
    return std::reverse_iterator<const_iterator>{cbegin()};
  }

  [[nodiscard]] auto required_widths() const -> widths_type {
    auto const cur_size = size();
    auto result = zero_widths();

    for (size_type i = 0; i < cur_size; ++i) {
      for_each_field([&]<size_type I>() {
        result[I] = std::max<std::uint8_t>(
            result[I], static_cast<std::uint8_t>(required_bits_encoded<I>(
                           layout_.template read_field<I>(i))));
      });
    }

    return result;
  }

  [[nodiscard]] auto required_bits() const -> size_type
    requires(field_count == 1)
  {
    return required_widths()[0];
  }

  void reset(widths_type const& widths = zero_widths(), size_type sz = 0) {
    check_size_limit(sz);
    destroy_heap_storage();
    initialize(checked_widths(widths), sz);
  }

  void reset(size_type bits, size_type sz = 0)
    requires(field_count == 1)
  {
    reset(make_widths(bits), sz);
  }

  void resize(size_type new_size, value_type value = value_type{}) {
    check_size_limit(new_size);

    auto const old_size = size();

    if (new_size > old_size) {
      auto const old_widths = widths();

      if constexpr (auto_bit_width) {
        ensure_widths(required_widths(value), new_size, old_size, old_widths);
      } else {
        ensure_capacity_for(new_size, old_widths, old_size);
      }

      field_descriptor::encode_with(value, [&]<size_type I>(auto encoded) {
        layout_.template fill_field<I>(old_size, new_size, encoded);
      });
    }

    layout_.set_size(new_size);
  }

  void reserve(size_type sz) {
    check_size_limit(sz);
    ensure_capacity_for(sz, widths(), size());
  }

  void shrink_to_fit() { repack_exact(widths(), size()); }

  void optimize_storage()
    requires auto_bit_width
  {
    repack_exact(required_widths(), size());
  }

  [[nodiscard]] auto is_inline() const noexcept -> bool {
    if constexpr (layout_type::supports_inline) {
      return layout_.is_inline();
    } else {
      return false;
    }
  }

  [[nodiscard]] auto uses_heap() const noexcept -> bool {
    return layout_.owns_heap_storage();
  }

  void truncate_to_widths(widths_type const& new_widths) {
    repack_exact(checked_widths(new_widths), size());
  }

  void truncate_to_bits(size_type new_bits)
    requires(field_count == 1)
  {
    truncate_to_widths(make_widths(new_bits));
  }

  [[nodiscard]] auto capacity() const noexcept -> size_type {
    return layout_.usable_capacity(widths());
  }

  [[nodiscard]] auto widths() const noexcept -> widths_type {
    return layout_.widths();
  }

  [[nodiscard]] auto bits() const noexcept -> size_type
    requires(field_count == 1)
  {
    return widths()[0];
  }

  void clear() noexcept { layout_.set_size(0); }

  [[nodiscard]] auto size() const noexcept -> size_type {
    return layout_.size();
  }

  [[nodiscard]] auto size_in_bytes() const noexcept -> size_type {
    return used_blocks() * sizeof(underlying_type);
  }

  [[nodiscard]] auto empty() const noexcept -> bool { return size() == 0; }

  auto operator[](size_type i) const -> const_reference { return get(i); }

  auto at(size_type i) const -> const_reference {
    if (i >= size()) {
      throw std::out_of_range("basic_packed_int_vector::at");
    }
    return get(i);
  }

  [[nodiscard]] auto get(size_type i) const -> const_reference {
    assert(i < size());
    return get_value(i);
  }

  template <size_type I>
  [[nodiscard]] auto get_field(size_type i) const ->
      typename field_descriptor::template field_value_type<I>
    requires(field_count > 1)
  {
    return get_field_value<I>(i);
  }

  auto operator[](size_type i) -> reference { return reference{*this, i}; }

  auto at(size_type i) -> reference {
    if (i >= size()) {
      throw std::out_of_range("basic_packed_int_vector::at");
    }
    return (*this)[i];
  }

  void set(size_type i, value_type value) {
    assert(i < size());
    set_value(i, value);
  }

  template <size_type I>
  void set_field(size_type i,
                 typename field_descriptor::template field_value_type<I> value)
    requires(field_count > 1)
  {
    set_field_value<I>(i, value);
  }

  void push_back(value_type value) {
    auto const old_size = size();

    if (old_size == max_size()) {
      throw_size_limit();
    }

    auto const new_size = old_size + 1;
    auto const old_widths = widths();

    if constexpr (auto_bit_width) {
      ensure_widths(required_widths(value), new_size, old_size, old_widths);
    } else {
      ensure_capacity_for(new_size, old_widths, old_size);
    }

    write_value(old_size, value);
    layout_.set_size(new_size);
  }

  void pop_back() noexcept {
    assert(!empty());
    layout_.set_size(size() - 1);
  }

  [[nodiscard]] auto back() const -> const_reference {
    assert(!empty());
    return get(size() - 1);
  }

  [[nodiscard]] auto back() -> reference {
    assert(!empty());
    return (*this)[size() - 1];
  }

  [[nodiscard]] auto front() const -> const_reference {
    assert(!empty());
    return get(0);
  }

  [[nodiscard]] auto front() -> reference {
    assert(!empty());
    return (*this)[0];
  }

  [[nodiscard]] auto unpack() const -> std::vector<value_type> {
    std::vector<value_type> result(size());
    for (size_type i = 0; i < size(); ++i) {
      result[i] = get(i);
    }
    return result;
  }

  iterator erase(const_iterator pos) {
    auto const index = pos.index_;
    auto const sz = size();
    assert(pos.vec_ == this);
    assert(index < sz);

    for (size_type i = index + 1; i < sz; ++i) {
      copy_encoded_fields(layout_, i - 1, layout_, i);
    }

    layout_.set_size(sz - 1);
    return iterator{this, index};
  }

  iterator erase(const_iterator first, const_iterator last) {
    auto const first_index = first.index_;
    auto const last_index = last.index_;
    auto const sz = size();

    assert(first.vec_ == this);
    assert(last.vec_ == this);
    assert(first_index <= last_index);
    assert(last_index <= sz);

    auto const num_to_erase = last_index - first_index;
    for (size_type i = last_index; i < sz; ++i) {
      copy_encoded_fields(layout_, i - num_to_erase, layout_, i);
    }

    layout_.set_size(sz - num_to_erase);
    return iterator{this, first_index};
  }

  iterator erase(iterator pos) {
    return erase(const_iterator{this, pos.index_});
  }

  iterator erase(iterator first, iterator last) {
    return erase(const_iterator{this, first.index_},
                 const_iterator{this, last.index_});
  }

  iterator insert(const_iterator pos, size_type count, value_type value) {
    auto const index = pos.index_;
    assert(pos.vec_ == this);

    auto req_widths = widths();
    if constexpr (auto_bit_width) {
      req_widths = required_widths(value);
    }

    return insert_known_n(index, count, req_widths,
                          [value]() { return value; });
  }

  iterator insert(const_iterator pos, value_type value) {
    return insert(pos, 1, value);
  }

  template <std::input_iterator InputIt>
  iterator insert(const_iterator pos, InputIt first, InputIt last) {
    auto const index = pos.index_;
    assert(pos.vec_ == this);

    if constexpr (std::forward_iterator<InputIt>) {
      auto req_widths = widths();
      size_type count = 0;

      if constexpr (auto_bit_width) {
        for (auto it = first; it != last; ++it) {
          req_widths = widened_widths(
              req_widths, required_widths(static_cast<value_type>(*it)));
          ++count;
        }
      } else {
        count = std::distance(first, last);
      }

      return insert_known_n(index, count, req_widths,
                            [first]() mutable { return *first++; });
    } else {
      return insert_single_pass(index, first, last);
    }
  }

  iterator insert(const_iterator pos, std::initializer_list<value_type> ilist) {
    return insert(pos, ilist.begin(), ilist.end());
  }

  void assign(size_type count, value_type value) {
    clear();
    insert(begin(), count, value);
  }

  template <std::input_iterator InputIt>
  void assign(InputIt first, InputIt last) {
    clear();
    insert(begin(), first, last);
  }

  void assign(std::initializer_list<value_type> ilist) {
    assign(ilist.begin(), ilist.end());
  }

  template <std::ranges::input_range R>
    requires std::convertible_to<std::ranges::range_reference_t<R>, value_type>
  iterator insert_range(const_iterator pos, R&& r) {
    auto&& range = std::forward<R>(r);
    return insert(pos, std::ranges::begin(range), std::ranges::end(range));
  }

  template <std::ranges::input_range R>
    requires std::convertible_to<std::ranges::range_reference_t<R>, value_type>
  void assign_range(R&& r) {
    auto&& range = std::forward<R>(r);
    assign(std::ranges::begin(range), std::ranges::end(range));
  }

  template <std::ranges::input_range R>
    requires std::convertible_to<std::ranges::range_reference_t<R>, value_type>
  void append_range(R&& r) {
    insert_range(end(), std::forward<R>(r));
  }

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

  [[nodiscard]] static constexpr auto zero_widths() noexcept -> widths_type {
    widths_type widths{};
    widths.fill(0);
    return widths;
  }

  [[nodiscard]] static constexpr auto
  make_widths(size_type bits) noexcept -> widths_type
    requires(field_count == 1)
  {
    return widths_type{static_cast<std::uint8_t>(bits)};
  }

  [[nodiscard]] static constexpr auto
  total_bits(widths_type const& widths) noexcept -> size_type {
    size_type total = 0;
    for (auto const bits : widths) {
      total += bits;
    }
    return total;
  }

  [[nodiscard]] static auto checked_widths(widths_type widths) -> widths_type {
    auto const max = max_widths();
    for (size_type i = 0; i < field_count; ++i) {
      if (widths[i] > max[i]) {
        throw std::invalid_argument(
            "basic_packed_int_vector: invalid bit width");
      }
    }
    return widths;
  }

  [[nodiscard]] static constexpr auto
  widened_widths(widths_type lhs, widths_type const& rhs) noexcept
      -> widths_type {
    for (size_type i = 0; i < field_count; ++i) {
      lhs[i] = std::max(lhs[i], rhs[i]);
    }
    return lhs;
  }

  template <typename F, size_type... I>
  static constexpr void for_each_field_impl(F&& f, std::index_sequence<I...>) {
    (std::forward<F>(f).template operator()<I>(), ...);
  }

  template <typename F>
  static constexpr void for_each_field(F&& f) {
    for_each_field_impl(std::forward<F>(f),
                        std::make_index_sequence<field_count>{});
  }

  template <typename DstLayout, typename SrcLayout, size_type... I>
  static void
  copy_encoded_fields_impl(DstLayout& dst, size_type dst_index,
                           SrcLayout const& src, size_type src_index,
                           std::index_sequence<I...>) {
    (dst.template write_field<I>(dst_index,
                                 src.template read_field<I>(src_index)),
     ...);
  }

  template <typename DstLayout, typename SrcLayout>
  static void copy_encoded_fields(DstLayout& dst, size_type dst_index,
                                  SrcLayout const& src, size_type src_index) {
    copy_encoded_fields_impl(dst, dst_index, src, src_index,
                             std::make_index_sequence<field_count>{});
  }

  template <size_type I>
  static constexpr auto
  required_bits_encoded(field_encoded_type<I> encoded) noexcept -> size_type {
    using encoded_type_i = field_encoded_type<I>;
    using unsigned_encoded_type_i = std::make_unsigned_t<encoded_type_i>;

    if (encoded == 0) {
      return 0;
    }

    auto const uvalue = static_cast<unsigned_encoded_type_i>(encoded);
    constexpr auto encoded_digits =
        std::numeric_limits<unsigned_encoded_type_i>::digits;

    if constexpr (std::is_signed_v<encoded_type_i>) {
      if (encoded > 0) {
        return encoded_digits - std::countl_zero(uvalue) + 1;
      }
      return encoded_digits - std::countl_one(uvalue) + 1;
    } else {
      return encoded_digits - std::countl_zero(uvalue);
    }
  }

  template <std::ranges::forward_range R>
    requires std::convertible_to<std::ranges::range_reference_t<R>, value_type>
  static auto required_widths_for(R&& r) -> widths_type {
    auto widths = zero_widths();
    auto const max = max_widths();

    for (auto&& v : std::forward<R>(r)) {
      widths =
          widened_widths(widths, required_widths(static_cast<value_type>(v)));

      if (widths == max) {
        break;
      }
    }

    return widths;
  }

  template <size_type I>
  [[nodiscard]] auto
  get_encoded_field(size_type i) const -> field_encoded_type<I> {
    assert(i < size());
    return layout_.template read_field<I>(i);
  }

  [[nodiscard]] auto get_value(size_type i) const -> value_type {
    assert(i < size());
    return field_descriptor::decode_with(
        [&]<size_type I>() { return get_encoded_field<I>(i); });
  }

  template <size_type I>
  [[nodiscard]] auto get_field_value(size_type i) const ->
      typename field_descriptor::template field_value_type<I> {
    assert(i < size());
    return field_traits_type<I>::decode(get_encoded_field<I>(i));
  }

  template <size_type I>
  [[nodiscard]] static auto required_widths_for_field_value(
      typename field_descriptor::template field_value_type<I> const& value)
      -> widths_type {
    auto req = zero_widths();
    auto const encoded = field_traits_type<I>::encode(value);
    req[I] = static_cast<std::uint8_t>(required_bits_encoded<I>(encoded));
    return req;
  }

  void ensure_assignment_widths(widths_type const& required) {
    if constexpr (auto_bit_width) {
      auto const cur_size = size();
      auto const old_widths = widths();
      auto const new_widths = widened_widths(required, old_widths);
      if (new_widths != old_widths) {
        ensure_widths(new_widths, cur_size, cur_size, old_widths);
      }
    }
  }

  template <size_type I>
  void set_field_value(
      size_type i,
      typename field_descriptor::template field_value_type<I> value) {
    assert(i < size());

    ensure_assignment_widths(required_widths_for_field_value<I>(value));

    auto const encoded = field_traits_type<I>::encode(value);
    layout_.template write_field<I>(i, encoded);
  }

  void write_value(size_type i, value_type const& value) {
    field_descriptor::encode_with(value, [&]<size_type I>(auto encoded) {
      layout_.template write_field<I>(i, encoded);
    });
  }

  void set_value(size_type i, value_type const& value) {
    assert(i < size());
    ensure_assignment_widths(required_widths(value));
    write_value(i, value);
  }

  template <typename Producer>
  iterator insert_known_n(size_type index, size_type count,
                          widths_type const& req_widths, Producer&& produce) {
    auto const old_size = size();
    assert(index <= old_size);

    if (count == 0) {
      return iterator{this, index};
    }

    if (count > max_size() || old_size > max_size() - count) {
      throw_size_limit();
    }

    auto const new_size = old_size + count;
    auto const old_widths = widths();

    if constexpr (auto_bit_width) {
      ensure_widths(req_widths, new_size, old_size, old_widths);
    } else {
      ensure_capacity_for(new_size, old_widths, old_size);
    }

    for (size_type i = old_size; i > index; --i) {
      copy_encoded_fields(layout_, i + count - 1, layout_, i - 1);
    }

    auto&& gen = std::forward<Producer>(produce);
    for (size_type i = 0; i < count; ++i) {
      write_value(index + i, static_cast<value_type>(gen()));
    }

    layout_.set_size(new_size);
    return iterator{this, index};
  }

  template <std::input_iterator I>
  iterator insert_single_pass(size_type idx, I first, I last) {
    auto const old_size = size();

    for (; first != last; ++first) {
      push_back(static_cast<value_type>(*first));
    }

    std::rotate(iterator{this, idx}, iterator{this, old_size}, end());
    return iterator{this, idx};
  }

  [[nodiscard]] auto
  initialize_heap_layout(layout_type& layout, widths_type const& widths,
                         size_type logical_size, size_type capacity_blocks,
                         init_mode init, bool force_allocation = false)
      -> underlying_type* {
    assert(layout_type::can_store_heap(widths, logical_size, capacity_blocks));

    auto* data = storage_type::allocate(logical_size, capacity_blocks, init,
                                        force_allocation);
    layout.set_heap_state(data, widths);
    return data;
  }

  template <packed_vector_bit_width_strategy OtherStrategy,
            typename OtherPolicy, typename OtherGrowthPolicy>
  void copy_from_impl(other_vector_type<OtherStrategy, OtherPolicy,
                                        OtherGrowthPolicy> const& other) {
    using other_type =
        other_vector_type<OtherStrategy, OtherPolicy, OtherGrowthPolicy>;
    using other_layout_type = typename other_type::layout_type;

    auto const src_size = other.size();
    auto const src_widths = other.widths();

    check_size_limit(src_size);

    if constexpr (same_layout_v<other_layout_type>) {
      if (!other.layout_.owns_heap_storage()) {
        destroy_heap_storage();
        layout_ = other.layout_;
        return;
      }
    }

    auto const copy_heap_blocks = other.layout_.owns_heap_storage();
    bool use_heap = true;

    if constexpr (layout_type::supports_inline) {
      use_heap = copy_heap_blocks ||
                 !layout_type::can_store_inline(src_widths, src_size);
    }

    layout_type new_layout{};

    if (use_heap) {
      auto const capacity_blocks =
          copy_heap_blocks ? other.layout_.capacity_blocks()
                           : exact_capacity_blocks(src_size, src_widths);

      auto* data = initialize_heap_layout(new_layout, src_widths, src_size,
                                          capacity_blocks, init_mode::no_init,
                                          copy_heap_blocks);

      if (copy_heap_blocks) {
        auto const blocks = other.used_blocks();
        if (blocks > 0) {
          storage_type::copy_n(other.layout_.heap_data(), blocks, data);
        }
      }
    } else {
      if constexpr (layout_type::supports_inline) {
        new_layout.set_inline_state(src_widths, src_size);
      }
    }

    if (!copy_heap_blocks && total_bits(src_widths) > 0) {
      for (size_type i = 0; i < src_size; ++i) {
        copy_encoded_fields(new_layout, i, other.layout_, i);
      }
    }

    destroy_heap_storage();
    layout_ = new_layout;
  }

  template <packed_vector_bit_width_strategy OtherStrategy,
            typename OtherPolicy, typename OtherGrowthPolicy>
  void move_from_impl(
      other_vector_type<OtherStrategy, OtherPolicy, OtherGrowthPolicy>&& other)
      noexcept(
          same_layout_v<typename other_vector_type<
              OtherStrategy, OtherPolicy, OtherGrowthPolicy>::layout_type>) {
    using other_type =
        other_vector_type<OtherStrategy, OtherPolicy, OtherGrowthPolicy>;
    using other_layout_type = typename other_type::layout_type;
    auto&& src = std::move(other);

    if constexpr (same_layout_v<other_layout_type>) {
      destroy_heap_storage();
      layout_ = src.layout_;
      src.layout_.reset_empty();
    } else {
      auto const src_size = src.size();
      auto const src_widths = src.widths();

      check_size_limit(src_size);

      if (src.layout_.owns_heap_storage()) {
        layout_type new_layout{};
        new_layout.set_heap_state(src.layout_.release_heap_data(), src_widths);

        destroy_heap_storage();
        layout_ = new_layout;
        src.layout_.reset_empty();
      } else {
        copy_from_impl(src);
        src.destroy_heap_storage();
      }
    }
  }

  [[nodiscard]] static constexpr auto
  min_data_size(size_type sz, widths_type const& widths) noexcept -> size_type {
    auto const stride_bits = total_bits(widths);
    if (stride_bits == 0) {
      return 0;
    }

    auto const q = sz / bits_per_block;
    auto const r = sz % bits_per_block;
    return q * stride_bits + detail::ceil_div(r * stride_bits, bits_per_block);
  }

  [[nodiscard]] static constexpr auto
  normalize_capacity_blocks(size_type blocks) noexcept -> size_type {
    return detail::round_up_to_multiple(
        blocks, layout_type::capacity_granularity_blocks);
  }

  [[nodiscard]] static constexpr auto
  exact_capacity_blocks(size_type sz, widths_type const& widths) noexcept
      -> size_type {
    return normalize_capacity_blocks(min_data_size(sz, widths));
  }

  [[nodiscard]] static auto
  grown_capacity_blocks(size_type current, size_type minimum) noexcept
      -> size_type {
    minimum = normalize_capacity_blocks(minimum);

    auto const grown = normalize_capacity_blocks(
        static_cast<size_type>(growth_policy_type{}(current, minimum)));

    return std::max(grown, minimum);
  }

  [[nodiscard]] auto used_blocks() const noexcept -> size_type {
    return min_data_size(size(), widths());
  }

  static constexpr void check_size_limit(size_type n) {
    if constexpr (max_size() < std::numeric_limits<size_type>::max()) {
      if (n > max_size()) {
        throw_size_limit();
      }
    }
  }

  [[noreturn]] static void throw_size_limit() {
    throw std::length_error(
        "basic_packed_int_vector: policy size limit exceeded");
  }

  [[nodiscard]] static auto
  select_heap_capacity_blocks(size_type current, size_type minimum)
      -> size_type {
    assert(minimum <= layout_type::max_capacity_blocks_value);

    auto const blocks = std::min(grown_capacity_blocks(current, minimum),
                                 layout_type::max_capacity_blocks_value);

    assert(blocks >= minimum);
    return blocks;
  }

  void destroy_heap_storage() noexcept {
    storage_type::deallocate(layout_.release_heap_data());
    layout_.reset_empty();
  }

  void initialize(widths_type const& widths, size_type sz) {
    if constexpr (layout_type::supports_inline) {
      if (layout_type::can_store_inline(widths, sz)) {
        layout_.set_inline_state(widths, sz);
        return;
      }
    }

    auto const blocks = exact_capacity_blocks(sz, widths);
    assert(layout_type::can_store_heap(widths, sz, blocks));

    auto* data = storage_type::allocate(sz, blocks, init_mode::zero_init);
    layout_.set_heap_state(data, widths);
  }

  void ensure_capacity_for(size_type new_size, widths_type const& new_widths,
                           size_type old_size) {
    if (new_size <= layout_.usable_capacity(new_widths)) {
      return;
    }

    auto const min_blocks = exact_capacity_blocks(new_size, new_widths);
    auto const new_blocks =
        select_heap_capacity_blocks(layout_.capacity_blocks(), min_blocks);

    rebuild_storage(new_widths, old_size, new_size, new_blocks);
  }

  void ensure_widths(widths_type const& needed_widths, size_type reserve_size,
                     size_type cur_size, widths_type const& old_widths)
    requires auto_bit_width
  {
    auto const new_widths = widened_widths(needed_widths, old_widths);

    if (new_widths == old_widths) {
      ensure_capacity_for(reserve_size, new_widths, cur_size);
      return;
    }

    if (reserve_size <= layout_.usable_capacity(new_widths)) {
      rebuild_storage(new_widths, cur_size, reserve_size,
                      layout_.capacity_blocks());
      return;
    }

    auto const min_blocks = exact_capacity_blocks(reserve_size, new_widths);
    auto const new_blocks =
        select_heap_capacity_blocks(layout_.capacity_blocks(), min_blocks);

    rebuild_storage(new_widths, cur_size, reserve_size, new_blocks);
  }

  [[nodiscard]] auto
  is_exact_representation(widths_type const& new_widths, size_type new_size,
                          size_type exact_blocks) const noexcept -> bool {
    if (new_widths != widths() || new_size != size()) {
      return false;
    }

    if constexpr (layout_type::supports_inline) {
      if (layout_type::can_store_inline(new_widths, new_size) !=
          layout_.is_inline()) {
        return false;
      }

      if (layout_.is_inline()) {
        return true;
      }
    }

    return layout_.capacity_blocks() == exact_blocks;
  }

  void repack_exact(widths_type const& new_widths, size_type new_size) {
    auto const exact_blocks = exact_capacity_blocks(new_size, new_widths);

    if (!is_exact_representation(new_widths, new_size, exact_blocks)) {
      rebuild_storage(new_widths, new_size, new_size, exact_blocks);
    }
  }

  void
  rebuild_storage(widths_type const& new_widths, size_type logical_size,
                  size_type reserved_size, size_type target_capacity_blocks) {
    auto const copy_size = std::min(size(), logical_size);
    auto const old_widths = widths();
    auto const old_total_bits = total_bits(old_widths);
    auto const new_total_bits = total_bits(new_widths);

    auto const must_materialize_zero_prefix =
        copy_size > 0 && old_total_bits == 0 && new_total_bits > 0;
    auto const must_copy_elements =
        copy_size > 0 && old_total_bits > 0 && new_total_bits > 0;

    layout_type new_layout{};
    bool layout_uses_heap = true;

    if constexpr (layout_type::supports_inline) {
      if (layout_type::can_store_inline(new_widths, reserved_size)) {
        new_layout.set_inline_state(new_widths, logical_size);
        layout_uses_heap = false;
      }
    }

    if (layout_uses_heap) {
      auto const min_blocks = exact_capacity_blocks(reserved_size, new_widths);
      auto const capacity_blocks = std::max(
          normalize_capacity_blocks(target_capacity_blocks), min_blocks);

      assert(layout_type::can_store_heap(new_widths, logical_size,
                                         capacity_blocks));

      auto const init = must_materialize_zero_prefix ? init_mode::zero_init
                                                     : init_mode::no_init;
      auto* new_data = storage_type::allocate(logical_size, capacity_blocks,
                                              init, reserved_size > 0);

      new_layout.set_heap_state(new_data, new_widths);
    }

    if (must_copy_elements) {
      for (size_type i = 0; i < copy_size; ++i) {
        copy_encoded_fields(new_layout, i, layout_, i);
      }
    }

    destroy_heap_storage();
    layout_ = new_layout;
  }

  layout_type layout_{};
};

} // namespace dwarfs::container
