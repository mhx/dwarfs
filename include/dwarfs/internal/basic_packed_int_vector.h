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
#include <bit>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <iterator>
#include <limits>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include <dwarfs/bit_view.h>

#include <dwarfs/internal/detail/block_storage.h>
#include <dwarfs/internal/detail/packed_vector_helpers.h>
#include <dwarfs/internal/detail/packed_vector_layout.h>
#include <dwarfs/internal/detail/vector_growth_policy.h>

namespace dwarfs::internal {

enum class packed_vector_bit_width_strategy {
  fixed,
  automatic,
};

template <typename T>
concept integral_but_not_bool = std::integral<T> && !std::same_as<T, bool>;

template <integral_but_not_bool T,
          packed_vector_bit_width_strategy BitWidthStrategy, typename Policy,
          typename GrowthPolicy = detail::default_block_growth_policy>
class basic_packed_int_vector {
 public:
  using value_type = T;
  using underlying_type = std::make_unsigned_t<T>;
  using size_type = std::size_t;
  using policy_type = Policy;
  using growth_policy_type = GrowthPolicy;

  static constexpr bool auto_bit_width =
      BitWidthStrategy == packed_vector_bit_width_strategy::automatic;
  static constexpr size_type bits_per_block =
      std::numeric_limits<underlying_type>::digits;

  template <integral_but_not_bool, packed_vector_bit_width_strategy, typename,
            typename>
  friend class basic_packed_int_vector;

  class value_proxy {
   public:
    value_proxy(basic_packed_int_vector& vec, size_type i)
        : vec_{vec}
        , i_{i} {}

    operator T() const { return vec_.get(i_); }

    value_proxy const& operator=(T value) const {
      vec_.set(i_, value);
      return *this;
    }

    value_proxy const& operator=(value_proxy const& other) const {
      return *this = static_cast<T>(other);
    }

    friend void swap(value_proxy a, value_proxy b) {
      T tmp = a;
      a = static_cast<T>(b);
      b = tmp;
    }

   private:
    basic_packed_int_vector& vec_;
    size_type i_;
  };

  class iterator {
   public:
    using iterator_concept = std::random_access_iterator_tag;
    using iterator_category = std::random_access_iterator_tag;
    using value_type = T;
    using difference_type = std::ptrdiff_t;
    using reference = value_proxy;

    iterator() = default;

    reference operator*() const { return (*vec_)[index_]; }

    reference operator[](difference_type n) const {
      return (*vec_)[index_ + n];
    }

    iterator& operator++() {
      ++index_;
      return *this;
    }

    iterator operator++(int) {
      auto tmp = *this;
      ++*this;
      return tmp;
    }

    iterator& operator--() {
      --index_;
      return *this;
    }

    iterator operator--(int) {
      auto tmp = *this;
      --*this;
      return tmp;
    }

    iterator& operator+=(difference_type n) {
      index_ += n;
      return *this;
    }

    iterator& operator-=(difference_type n) {
      index_ -= n;
      return *this;
    }

    friend iterator operator+(iterator it, difference_type n) {
      it += n;
      return it;
    }

    friend iterator operator+(difference_type n, iterator it) {
      it += n;
      return it;
    }

    friend iterator operator-(iterator it, difference_type n) {
      it -= n;
      return it;
    }

    friend difference_type operator-(iterator a, iterator b) {
      return static_cast<difference_type>(a.index_) -
             static_cast<difference_type>(b.index_);
    }

    friend bool operator==(iterator a, iterator b) {
      return a.vec_ == b.vec_ && a.index_ == b.index_;
    }

    friend auto operator<=>(iterator a, iterator b) {
      assert(a.vec_ == b.vec_);
      return a.index_ <=> b.index_;
    }

    friend T iter_move(iterator const& it) noexcept {
      return it.vec_->get(it.index_);
    }

    friend void iter_swap(iterator const& a, iterator const& b) {
      using std::swap;
      swap((*a.vec_)[a.index_], (*b.vec_)[b.index_]);
    }

   private:
    friend class basic_packed_int_vector;

    iterator(basic_packed_int_vector* vec, size_type index)
        : vec_{vec}
        , index_{index} {}

    basic_packed_int_vector* vec_{nullptr};
    size_type index_{0};
  };

  class const_iterator {
   public:
    using iterator_concept = std::random_access_iterator_tag;
    using iterator_category = std::random_access_iterator_tag;
    using value_type = T;
    using difference_type = std::ptrdiff_t;
    using reference = T;

    const_iterator() = default;

    const_iterator(iterator it)
        : vec_{it.vec_}
        , index_{it.index_} {}

    reference operator*() const { return (*vec_)[index_]; }

    reference operator[](difference_type n) const {
      return (*vec_)[index_ + n];
    }

    const_iterator& operator++() {
      ++index_;
      return *this;
    }

    const_iterator operator++(int) {
      auto tmp = *this;
      ++*this;
      return tmp;
    }

    const_iterator& operator--() {
      --index_;
      return *this;
    }

    const_iterator operator--(int) {
      auto tmp = *this;
      --*this;
      return tmp;
    }

    const_iterator& operator+=(difference_type n) {
      index_ += n;
      return *this;
    }

    const_iterator& operator-=(difference_type n) {
      index_ -= n;
      return *this;
    }

    friend const_iterator operator+(const_iterator it, difference_type n) {
      it += n;
      return it;
    }

    friend const_iterator operator+(difference_type n, const_iterator it) {
      it += n;
      return it;
    }

    friend const_iterator operator-(const_iterator it, difference_type n) {
      it -= n;
      return it;
    }

    friend difference_type operator-(const_iterator a, const_iterator b) {
      return static_cast<difference_type>(a.index_) -
             static_cast<difference_type>(b.index_);
    }

    friend bool operator==(const_iterator a, const_iterator b) {
      return a.vec_ == b.vec_ && a.index_ == b.index_;
    }

    friend auto operator<=>(const_iterator a, const_iterator b) {
      assert(a.vec_ == b.vec_);
      return a.index_ <=> b.index_;
    }

    friend T iter_move(const_iterator const& it) noexcept {
      return it.vec_->get(it.index_);
    }

   private:
    friend class basic_packed_int_vector;

    const_iterator(basic_packed_int_vector const* vec, size_type index)
        : vec_{vec}
        , index_{index} {}

    basic_packed_int_vector const* vec_{nullptr};
    size_type index_{0};
  };

 private:
  using storage_type = detail::raw_block_storage<underlying_type>;
  using init_mode = typename storage_type::initialization;
  using layout_type =
      detail::packed_vector_layout<policy_type, underlying_type>;
  template <packed_vector_bit_width_strategy OtherStrategy,
            typename OtherPolicy, typename OtherGrowthPolicy>
  using other_vector_type =
      basic_packed_int_vector<T, OtherStrategy, OtherPolicy, OtherGrowthPolicy>;
  template <typename OtherLayout>
  static constexpr bool same_layout_v = std::same_as<layout_type, OtherLayout>;

 public:
  static constexpr bool has_inline_storage = layout_type::supports_inline;

  static constexpr size_type max_size() noexcept {
    return layout_type::max_heap_size;
  }

  static constexpr size_type inline_capacity_for_bits(size_type bits) noexcept
    requires has_inline_storage
  {
    return layout_type::inline_capacity_for_bits(bits);
  }

  static void dump_layout(std::ostream& os) { layout_type::dump(os); }

  basic_packed_int_vector() = default;

  explicit basic_packed_int_vector(size_type bits) {
    initialize(checked_bits(bits), 0);
  }

  basic_packed_int_vector(size_type bits, size_type size) {
    check_size_limit(size);
    initialize(checked_bits(bits), size);
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
      basic_packed_int_vector<T, OtherStrategy, OtherPolicy,
                              OtherGrowthPolicy> const& other) {
    copy_from_impl(other);
  }

  template <packed_vector_bit_width_strategy OtherStrategy,
            typename OtherPolicy, typename OtherGrowthPolicy>
  basic_packed_int_vector(
      basic_packed_int_vector<T, OtherStrategy, OtherPolicy,
                              OtherGrowthPolicy>&&
          other) noexcept(same_layout_v<typename basic_packed_int_vector<T,
                                                                         OtherStrategy,
                                                                         OtherPolicy,
                                                                         OtherGrowthPolicy>::
                                            layout_type>) {
    move_from_impl(std::move(other));
  }

  template <packed_vector_bit_width_strategy OtherStrategy,
            typename OtherPolicy, typename OtherGrowthPolicy>
  auto operator=(basic_packed_int_vector<T, OtherStrategy, OtherPolicy,
                                         OtherGrowthPolicy> const& other)
      -> basic_packed_int_vector& {
    basic_packed_int_vector tmp;
    tmp.copy_from_impl(other);
    swap(tmp);
    return *this;
  }

  template <packed_vector_bit_width_strategy OtherStrategy,
            typename OtherPolicy, typename OtherGrowthPolicy>
  auto operator=(basic_packed_int_vector<T, OtherStrategy, OtherPolicy,
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

  static constexpr auto required_bits(T value) noexcept -> size_type {
    if (value == 0) {
      return 0;
    }

    auto const uvalue = static_cast<underlying_type>(value);

    if constexpr (std::is_signed_v<T>) {
      if (value > 0) {
        return bits_per_block - std::countl_zero(uvalue) + 1;
      }
      return bits_per_block - std::countl_one(uvalue) + 1;
    } else {
      return bits_per_block - std::countl_zero(uvalue);
    }
  }

  [[nodiscard]] auto required_bits() const -> size_type {
    auto const cur_size = size();
    size_type result = 0;
    for (size_type i = 0; i < cur_size && result < bits_per_block; ++i) {
      result = std::max(result, required_bits(get(i)));
    }
    return result;
  }

  void reset(size_type bits = 0, size_type sz = 0) {
    check_size_limit(sz);
    destroy_heap_storage();
    initialize(checked_bits(bits), sz);
  }

  void resize(size_type new_size, T value = T{}) {
    check_size_limit(new_size);

    auto const old_size = size();

    if (new_size > old_size) {
      auto const old_bits = bits();

      if constexpr (auto_bit_width) {
        ensure_bits(required_bits(value), new_size, old_size, old_bits);
      }

      ensure_capacity_for(new_size, old_bits, old_size);
      layout_.fill(old_size, new_size, value);
    }

    layout_.set_size(new_size);
  }

  void reserve(size_type sz) {
    check_size_limit(sz);
    ensure_capacity_for(sz, bits());
  }

  void shrink_to_fit() { repack_exact(bits(), size()); }

  void optimize_storage()
    requires auto_bit_width
  {
    repack_exact(required_bits(), size());
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

  void truncate_to_bits(size_type new_bits) {
    repack_exact(checked_bits(new_bits), size());
  }

  [[nodiscard]] auto capacity() const noexcept -> size_type {
    return layout_.usable_capacity(bits());
  }

  void clear() noexcept { layout_.set_size(0); }

  [[nodiscard]] auto size() const noexcept -> size_type {
    return layout_.size();
  }

  [[nodiscard]] auto bits() const noexcept -> size_type {
    return layout_.bits();
  }

  [[nodiscard]] auto size_in_bytes() const noexcept -> size_type {
    return used_blocks() * sizeof(underlying_type);
  }

  [[nodiscard]] auto empty() const noexcept -> bool { return size() == 0; }

  auto operator[](size_type i) const -> T { return get(i); }

  auto at(size_type i) const -> T {
    if (i >= size()) {
      throw std::out_of_range("basic_packed_int_vector::at");
    }
    return get(i);
  }

  [[nodiscard]] auto get(size_type i) const -> T {
    return layout_.template read<T>(i);
  }

  auto operator[](size_type i) -> value_proxy { return value_proxy{*this, i}; }

  auto at(size_type i) -> value_proxy {
    if (i >= size()) {
      throw std::out_of_range("basic_packed_int_vector::at");
    }
    return (*this)[i];
  }

  void set(size_type i, T value) {
    if constexpr (auto_bit_width) {
      auto const cur_size = size();
      ensure_bits(required_bits(value), cur_size, cur_size, bits());
    }

    layout_.write(i, value);
  }

  void push_back(T value) {
    auto const old_size = size();

    if (old_size == max_size()) {
      throw_size_limit();
    }

    auto const new_size = old_size + 1;
    auto cur_bits = bits();

    if constexpr (auto_bit_width) {
      auto const req_bits = required_bits(value);
      ensure_bits(req_bits, new_size, old_size, cur_bits);
      cur_bits = std::max(cur_bits, req_bits);
    }

    ensure_capacity_for(new_size, cur_bits, old_size);
    layout_.write(old_size, value);
    layout_.set_size(new_size);
  }

  void pop_back() noexcept {
    assert(!empty());
    layout_.set_size(size() - 1);
  }

  [[nodiscard]] auto back() const -> T { return get(size() - 1); }

  [[nodiscard]] auto back() -> value_proxy { return (*this)[size() - 1]; }

  [[nodiscard]] auto front() const -> T { return get(0); }

  [[nodiscard]] auto front() -> value_proxy { return (*this)[0]; }

  [[nodiscard]] auto unpack() const -> std::vector<T> {
    std::vector<T> result(size());
    for (size_type i = 0; i < size(); ++i) {
      result[i] = get(i);
    }
    return result;
  }

 private:
  [[nodiscard]] static auto
  representable_capacity_blocks(size_type source_capacity_blocks,
                                size_type used_blocks) noexcept
      -> std::optional<size_type> {
    auto blocks = std::min(source_capacity_blocks,
                           layout_type::max_capacity_blocks_value);

    if constexpr (layout_type::capacity_granularity_blocks > 1) {
      blocks -= blocks % layout_type::capacity_granularity_blocks;
    }

    if (blocks < used_blocks) {
      return std::nullopt;
    }

    return blocks;
  }

  template <packed_vector_bit_width_strategy OtherStrategy,
            typename OtherPolicy, typename OtherGrowthPolicy>
  void copy_from_impl(other_vector_type<OtherStrategy, OtherPolicy,
                                        OtherGrowthPolicy> const& other) {
    using other_type =
        other_vector_type<OtherStrategy, OtherPolicy, OtherGrowthPolicy>;
    using other_layout_type = typename other_type::layout_type;

    auto const src_size = other.size();
    auto const src_bits = other.bits();

    check_size_limit(src_size);
    assert(src_bits <= bits_per_block);

    // Fast path: identical representation and no owned heap storage.
    if constexpr (same_layout_v<other_layout_type>) {
      if (!other.layout_.owns_heap_storage()) {
        destroy_heap_storage();
        layout_ = other.layout_;
        return;
      }
    }

    layout_type new_layout{};

    bool use_heap = true;
    bool copy_heap_blocks = false;
    size_type capacity_blocks = 0;
    size_type used_blocks = 0;

    if (other.layout_.owns_heap_storage()) {
      used_blocks = other.used_blocks();

      if (auto tracked_capacity = representable_capacity_blocks(
              other.layout_.capacity_blocks(), used_blocks)) {
        capacity_blocks = *tracked_capacity;
        copy_heap_blocks = true;
      }
    }

    if constexpr (layout_type::supports_inline) {
      if (!copy_heap_blocks &&
          layout_type::can_store_inline(src_bits, src_size)) {
        new_layout.set_inline_state(src_bits, src_size);
        use_heap = false;
      }
    }

    if (use_heap) {
      if (!copy_heap_blocks) {
        capacity_blocks = exact_capacity_blocks(src_size, src_bits);
      }

      assert(layout_type::can_store_heap(src_bits, src_size, capacity_blocks));

      auto* data =
          capacity_blocks > 0
              ? storage_type::allocate(capacity_blocks, init_mode::no_init)
              : nullptr;

      if (copy_heap_blocks && used_blocks > 0) {
        storage_type::copy_n(other.layout_.heap_data(), used_blocks, data);
      }

      new_layout.set_heap_state(data, src_bits, src_size, capacity_blocks);
    }

    if (!copy_heap_blocks && src_bits > 0) {
      for (size_type i = 0; i < src_size; ++i) {
        new_layout.write(i, other.get(i));
      }
    }

    destroy_heap_storage();
    layout_ = new_layout;
  }

  template <packed_vector_bit_width_strategy OtherStrategy,
            typename OtherPolicy, typename OtherGrowthPolicy>
  void move_from_impl(
      other_vector_type<OtherStrategy, OtherPolicy, OtherGrowthPolicy>&&
          other) noexcept(same_layout_v<typename other_vector_type<OtherStrategy,
                                                                   OtherPolicy,
                                                                   OtherGrowthPolicy>::
                                            layout_type>) {
    using other_type =
        other_vector_type<OtherStrategy, OtherPolicy, OtherGrowthPolicy>;
    using other_layout_type = typename other_type::layout_type;

    // Fast path: identical layout representation.
    if constexpr (same_layout_v<other_layout_type>) {
      destroy_heap_storage();
      layout_ = other.layout_;
      other.layout_.reset_empty();
    } else {
      auto const src_size = other.size();
      auto const src_bits = other.bits();

      check_size_limit(src_size);
      assert(src_bits <= bits_per_block);

      bool can_steal_heap = false;
      size_type capacity_blocks = 0;

      if (other.layout_.owns_heap_storage()) {
        auto const used_blocks = other.used_blocks();

        if (auto tracked_capacity = representable_capacity_blocks(
                other.layout_.capacity_blocks(), used_blocks)) {
          capacity_blocks = *tracked_capacity;
          can_steal_heap = true;
        }
      }

      if (can_steal_heap) {
        auto* data = other.layout_.release_heap_data();

        layout_type new_layout{};
        new_layout.set_heap_state(data, src_bits, src_size, capacity_blocks);

        destroy_heap_storage();
        layout_ = new_layout;
        other.layout_.reset_empty();
      } else {
        // Fallback: copy logical contents, then clear source
        copy_from_impl(other);
        other.destroy_heap_storage();
      }
    }
  }

  [[nodiscard]] static auto checked_bits(size_type bits) -> size_type {
    if (bits > bits_per_block) {
      throw std::invalid_argument("basic_packed_int_vector: invalid bit width");
    }
    return bits;
  }

  [[nodiscard]] static constexpr auto
  min_data_size(size_type sz, size_type bits) noexcept -> size_type {
    if (bits == 0) {
      return 0;
    }

    // avoid overflow in multiplication; bits_per_block is a power of two,
    // so division is cheap
    auto const q = sz / bits_per_block;
    auto const r = sz % bits_per_block;

    return q * bits + detail::ceil_div(r * bits, bits_per_block);
  }

  [[nodiscard]] static constexpr auto
  normalize_capacity_blocks(size_type blocks) noexcept -> size_type {
    return detail::round_up_to_multiple(
        blocks, layout_type::capacity_granularity_blocks);
  }

  [[nodiscard]] static constexpr auto
  exact_capacity_blocks(size_type sz, size_type bits) noexcept -> size_type {
    return normalize_capacity_blocks(min_data_size(sz, bits));
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
    return min_data_size(size(), bits());
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

  void initialize(size_type bits, size_type sz) {
    if constexpr (layout_type::supports_inline) {
      if (layout_type::can_store_inline(bits, sz)) {
        layout_.set_inline_state(bits, sz);
        return;
      }
    }

    auto const blocks = exact_capacity_blocks(sz, bits);
    assert(layout_type::can_store_heap(bits, sz, blocks));

    auto* data = blocks > 0
                     ? storage_type::allocate(blocks, init_mode::zero_init)
                     : nullptr;
    layout_.set_heap_state(data, bits, sz, blocks);
  }

  void ensure_capacity_for(size_type new_size, size_type new_bits,
                           std::optional<size_type> old_size = std::nullopt) {
    if (new_size <= layout_.usable_capacity(new_bits)) {
      return;
    }

    auto const min_blocks = exact_capacity_blocks(new_size, new_bits);
    auto const new_blocks =
        select_heap_capacity_blocks(layout_.capacity_blocks(), min_blocks);

    rebuild_storage(new_bits, old_size.value_or(size()), new_size, new_blocks);
  }

  void ensure_bits(size_type needed_bits, size_type reserve_size,
                   size_type cur_size, size_type old_bits)
    requires auto_bit_width
  {
    auto const new_bits = std::max(old_bits, needed_bits);

    if (new_bits == old_bits) {
      ensure_capacity_for(reserve_size, new_bits, cur_size);
      return;
    }

    if (reserve_size <= layout_.usable_capacity(new_bits)) {
      rebuild_storage(new_bits, cur_size, reserve_size,
                      layout_.capacity_blocks());
      return;
    }

    auto const min_blocks = exact_capacity_blocks(reserve_size, new_bits);
    auto const new_blocks =
        select_heap_capacity_blocks(layout_.capacity_blocks(), min_blocks);

    rebuild_storage(new_bits, cur_size, reserve_size, new_blocks);
  }

  [[nodiscard]] auto
  is_exact_representation(size_type new_bits, size_type new_size,
                          size_type exact_blocks) const noexcept -> bool {
    if (new_bits != bits() || new_size != size()) {
      return false;
    }

    if constexpr (layout_type::supports_inline) {
      if (layout_type::can_store_inline(new_bits, new_size) !=
          layout_.is_inline()) {
        return false;
      }

      // Non-inline exact representations:
      // - real heap-backed storage with exact block count
      // - compact zero-bit non-inline/non-heap state
      return !layout_.owns_heap_storage() ||
             layout_.capacity_blocks() == exact_blocks;
    } else {
      return layout_.capacity_blocks() == exact_blocks;
    }
  }

  void repack_exact(size_type new_bits, size_type new_size) {
    auto const exact_blocks = exact_capacity_blocks(new_size, new_bits);

    if (!is_exact_representation(new_bits, new_size, exact_blocks)) {
      rebuild_storage(new_bits, new_size, new_size, exact_blocks);
    }
  }

  void
  rebuild_storage(size_type new_bits, size_type new_size,
                  size_type capacity_size, size_type target_capacity_blocks) {
    auto const copy_size = std::min(size(), new_size);
    auto const old_bits = bits();
    auto const must_materialize_zero_prefix =
        copy_size > 0 && old_bits == 0 && new_bits > 0;
    auto const must_copy_elements =
        copy_size > 0 && old_bits > 0 && new_bits > 0;

    layout_type new_layout;

    bool layout_uses_heap{true};

    if constexpr (layout_type::supports_inline) {
      if (layout_type::can_store_inline(new_bits, capacity_size)) {
        new_layout.set_inline_state(new_bits, new_size);
        layout_uses_heap = false;
      }
    }

    if (layout_uses_heap) {
      auto const min_blocks = exact_capacity_blocks(capacity_size, new_bits);
      auto const capacity_blocks = std::max(
          normalize_capacity_blocks(target_capacity_blocks), min_blocks);

      assert(layout_type::can_store_heap(new_bits, new_size, capacity_blocks));

      auto const init = must_materialize_zero_prefix ? init_mode::zero_init
                                                     : init_mode::no_init;
      auto* new_data = capacity_blocks > 0
                           ? storage_type::allocate(capacity_blocks, init)
                           : nullptr;

      new_layout.set_heap_state(new_data, new_bits, new_size, capacity_blocks);
    }

    if (must_copy_elements) {
      for (size_type i = 0; i < copy_size; ++i) {
        new_layout.write(i, get(i));
      }
    }

    destroy_heap_storage();

    layout_ = new_layout;
  }

  layout_type layout_{};
};

} // namespace dwarfs::internal
