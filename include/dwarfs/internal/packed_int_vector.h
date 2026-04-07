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
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include <dwarfs/bit_view.h>

#include <dwarfs/internal/detail/block_storage.h>
#include <dwarfs/internal/detail/default_packed_vector_metadata.h>
#include <dwarfs/internal/detail/vector_growth_policy.h>

namespace dwarfs::internal {

template <typename T>
concept integral_but_not_bool = std::integral<T> && !std::same_as<T, bool>;

template <integral_but_not_bool T, bool AutoBitWidth = false,
          typename Metadata = detail::default_packed_vector_metadata<>,
          typename GrowthPolicy = detail::default_block_growth_policy>
class basic_packed_int_vector {
 public:
  using value_type = T;
  using underlying_type = std::make_unsigned_t<T>;
  using size_type = std::size_t;
  using metadata_type = Metadata;
  using growth_policy_type = GrowthPolicy;

  static constexpr bool auto_bit_width = AutoBitWidth;
  static constexpr size_type bits_per_block{
      std::numeric_limits<underlying_type>::digits};

  class value_proxy {
   public:
    value_proxy(basic_packed_int_vector& vec, size_type i)
        : vec_{vec}
        , i_{i} {}

    operator T() const { return vec_.get(i_); }

    value_proxy& operator=(T value) {
      vec_.set(i_, value);
      return *this;
    }

   private:
    basic_packed_int_vector& vec_;
    size_type i_;
  };

  basic_packed_int_vector() = default;

  explicit basic_packed_int_vector(size_type bits) {
    set_bits_value(checked_bits(bits));
  }

  basic_packed_int_vector(size_type bits, size_type size) {
    bits = checked_bits(bits);
    auto const blocks = min_data_size(size, bits);

    data_ = storage_type::allocate(blocks, init_mode::zero_init);
    set_size_value(size);
    set_bits_value(bits);
    set_capacity_blocks_value(blocks);
  }

  ~basic_packed_int_vector() { storage_type::deallocate(data_); }

  basic_packed_int_vector(basic_packed_int_vector const& other) {
    auto const blocks = other.used_blocks();

    data_ = storage_type::allocate(blocks, init_mode::no_init);
    if (blocks > 0) {
      storage_type::copy_n(other.data_, blocks, data_);
    }

    set_size_value(other.size());
    set_bits_value(other.bits());
    set_capacity_blocks_value(blocks);
  }

  basic_packed_int_vector(basic_packed_int_vector&& other) noexcept
      : meta_{other.meta_}
      , data_{std::exchange(other.data_, nullptr)} {
    other.meta_ = metadata_type{};
  }

  basic_packed_int_vector& operator=(basic_packed_int_vector const& other) {
    if (this != &other) {
      basic_packed_int_vector tmp(other);
      swap(tmp);
    }
    return *this;
  }

  basic_packed_int_vector& operator=(basic_packed_int_vector&& other) noexcept {
    if (this != &other) {
      storage_type::deallocate(data_);
      data_ = std::exchange(other.data_, nullptr);
      meta_ = other.meta_;
      other.meta_ = metadata_type{};
    }
    return *this;
  }

  void swap(basic_packed_int_vector& other) noexcept {
    using std::swap;
    swap(meta_, other.meta_);
    swap(data_, other.data_);
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
    size_type result = 0;
    for (size_type i = 0; i < size() && result < bits_per_block; ++i) {
      result = std::max(result, required_bits(get(i)));
    }
    return result;
  }

  void reset(size_type bits = 0, size_type size = 0) {
    bits = checked_bits(bits);
    auto const blocks = min_data_size(size, bits);
    auto* new_data = storage_type::allocate(blocks, init_mode::zero_init);

    storage_type::deallocate(data_);
    data_ = new_data;
    set_size_value(size);
    set_bits_value(bits);
    set_capacity_blocks_value(blocks);
  }

  void resize(size_type new_size, T value = T{}) {
    auto const old_size = size();

    if (new_size > old_size) {
      if constexpr (AutoBitWidth) {
        ensure_bits(required_bits(value), new_size);
      }

      reserve_blocks(min_data_size(new_size, bits()));
      fill_values(data_, bits(), old_size, new_size, value);
    }

    set_size_value(new_size);
  }

  void reserve(size_type size) { reserve_blocks(min_data_size(size, bits())); }

  void shrink_to_fit() { shrink_to_fit_blocks(); }

  void optimize_storage()
    requires AutoBitWidth
  {
    auto const new_bits = required_bits();
    if (new_bits == bits()) {
      shrink_to_fit_blocks();
    } else {
      repack_data(new_bits, size());
    }
  }

  void truncate_to_bits(size_type new_bits) {
    new_bits = checked_bits(new_bits);
    if (new_bits != bits()) {
      repack_data(new_bits, size());
    }
  }

  [[nodiscard]] auto capacity() const noexcept -> size_type {
    return bits() > 0 ? (capacity_blocks_value() * bits_per_block) / bits() : 0;
  }

  void clear() noexcept { set_size_value(0); }

  [[nodiscard]] auto size() const noexcept -> size_type { return size_value(); }

  [[nodiscard]] auto bits() const noexcept -> size_type { return bits_value(); }

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
    return bits() > 0
               ? const_bit_view(data_).template read<T>({i * bits(), bits()})
               : T{};
  }

  auto operator[](size_type i) -> value_proxy { return value_proxy{*this, i}; }

  auto at(size_type i) -> value_proxy {
    if (i >= size()) {
      throw std::out_of_range("basic_packed_int_vector::at");
    }
    return (*this)[i];
  }

  void set(size_type i, T value) {
    if constexpr (AutoBitWidth) {
      ensure_bits(required_bits(value), size());
    }

    write_value(data_, bits(), i, value);
  }

  void push_back(T value) {
    auto const new_size = size() + 1;

    if constexpr (AutoBitWidth) {
      ensure_bits(required_bits(value), new_size);
    }

    reserve_blocks(min_data_size(new_size, bits()));
    write_value(data_, bits(), size(), value);
    set_size_value(new_size);
  }

  void pop_back() noexcept {
    assert(!empty());
    set_size_value(size() - 1);
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
  using storage_type = detail::raw_block_storage<underlying_type>;
  using init_mode = typename storage_type::initialization;

  [[nodiscard]] static auto checked_bits(size_type bits) -> size_type {
    if (bits > bits_per_block) {
      throw std::invalid_argument("basic_packed_int_vector: invalid bit width");
    }
    return bits;
  }

  [[nodiscard]] static constexpr auto
  min_data_size(size_type size, size_type bits) noexcept -> size_type {
    return bits == 0 ? 0 : (size * bits + bits_per_block - 1) / bits_per_block;
  }

  [[nodiscard]] auto size_value() const noexcept -> size_type {
    return static_cast<size_type>(meta_.size());
  }

  [[nodiscard]] auto bits_value() const noexcept -> size_type {
    return static_cast<size_type>(meta_.bits());
  }

  [[nodiscard]] auto capacity_blocks_value() const noexcept -> size_type {
    return static_cast<size_type>(meta_.capacity_blocks());
  }

  void set_size_value(size_type v) noexcept { meta_.set_size(v); }

  void set_bits_value(size_type v) noexcept { meta_.set_bits(v); }

  void set_capacity_blocks_value(size_type v) noexcept {
    meta_.set_capacity_blocks(v);
  }

  [[nodiscard]] auto used_blocks() const noexcept -> size_type {
    return min_data_size(size(), bits());
  }

  [[nodiscard]] static auto
  grown_capacity_blocks(size_type current, size_type minimum) noexcept
      -> size_type {
    return static_cast<size_type>(growth_policy_type{}(current, minimum));
  }

  static void
  write_value(underlying_type* data, size_type bits, size_type i, T value) {
    if (bits > 0) {
      bit_view(data).write({i * bits, bits}, value);
    }
  }

  static void fill_values(underlying_type* data, size_type bits,
                          size_type first, size_type last, T value) {
    if (bits == 0) {
      return;
    }

    auto view = bit_view(data);
    for (size_type i = first; i < last; ++i) {
      view.write({i * bits, bits}, value);
    }
  }

  void reserve_blocks(size_type min_capacity_blocks) {
    if (min_capacity_blocks <= capacity_blocks_value()) {
      return;
    }

    auto const new_capacity =
        grown_capacity_blocks(capacity_blocks_value(), min_capacity_blocks);

    auto* new_data = storage_type::allocate(new_capacity, init_mode::no_init);

    auto const blocks_to_copy = used_blocks();
    if (blocks_to_copy > 0) {
      storage_type::copy_n(data_, blocks_to_copy, new_data);
    }

    storage_type::deallocate(data_);
    data_ = new_data;
    set_capacity_blocks_value(new_capacity);
  }

  void shrink_to_fit_blocks() {
    auto const new_capacity = used_blocks();
    if (new_capacity == capacity_blocks_value()) {
      return;
    }

    underlying_type* new_data = nullptr;
    if (new_capacity > 0) {
      new_data = storage_type::allocate(new_capacity, init_mode::no_init);
      storage_type::copy_n(data_, new_capacity, new_data);
    }

    storage_type::deallocate(data_);
    data_ = new_data;
    set_capacity_blocks_value(new_capacity);
  }

  void ensure_bits(size_type needed_bits, size_type new_size)
    requires AutoBitWidth
  {
    auto const new_bits = std::max(bits(), needed_bits);
    if (new_bits != bits()) {
      repack_data(new_bits, new_size);
    }
  }

  void repack_data(size_type new_bits, size_type new_size) {
    auto const new_blocks = min_data_size(new_size, new_bits);
    auto const copy_size = std::min(size(), new_size);

    // If bits_ was 0, we skip copying the old data (since it is all zeroes),
    // so we *must* zero-initialize the new storage here.
    auto const init = bits() == 0 && copy_size > 0 ? init_mode::zero_init
                                                   : init_mode::no_init;
    auto* new_data = storage_type::allocate(new_blocks, init);

    if (new_bits != 0 && bits() != 0 && copy_size > 0) {
      auto src = const_bit_view(data_);
      auto dst = bit_view(new_data);

      for (size_type i = 0; i < copy_size; ++i) {
        dst.write({i * new_bits, new_bits},
                  src.template read<T>({i * bits(), bits()}));
      }
    }

    storage_type::deallocate(data_);
    data_ = new_data;
    set_bits_value(new_bits);
    set_capacity_blocks_value(new_blocks);
  }

  metadata_type meta_{};
  underlying_type* data_{nullptr};
};

template <integral_but_not_bool T>
using packed_int_vector = basic_packed_int_vector<T, false>;

template <integral_but_not_bool T>
using auto_packed_int_vector = basic_packed_int_vector<T, true>;

} // namespace dwarfs::internal
