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
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include <dwarfs/bit_view.h>

#include <dwarfs/internal/detail/block_storage.h>

namespace dwarfs::internal {

namespace detail {

template <std::integral T, bool AutoBitWidth = false>
  requires(!std::same_as<T, bool>)
class basic_packed_int_vector {
 public:
  using value_type = T;
  using underlying_type = std::make_unsigned_t<T>;
  using size_type = std::size_t;

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

  explicit basic_packed_int_vector(size_type bits)
      : bits_{checked_bits(bits)} {}

  basic_packed_int_vector(size_type bits, size_type size)
      : size_{size}
      , bits_{checked_bits(bits)}
      , data_(storage_type::zeroed(used_blocks())) {}

  basic_packed_int_vector(basic_packed_int_vector const& other)
      : size_{other.size_}
      , bits_{other.bits_}
      , data_{other.data_.clone_prefix(other.used_blocks())} {}

  basic_packed_int_vector& operator=(basic_packed_int_vector const& other) {
    if (this != &other) {
      basic_packed_int_vector tmp(other);
      swap(tmp);
    }
    return *this;
  }

  basic_packed_int_vector(basic_packed_int_vector&&) = default;
  basic_packed_int_vector& operator=(basic_packed_int_vector&&) = default;

  void swap(basic_packed_int_vector& other) noexcept {
    using std::swap;
    swap(size_, other.size_);
    swap(bits_, other.bits_);
    data_.swap(other.data_);
  }

  static constexpr size_type required_bits(T value) noexcept {
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

  size_type required_bits() const {
    size_type result = 0;
    for (size_type i = 0; i < size_ && result < bits_per_block; ++i) {
      result = std::max(result, required_bits(get(i)));
    }
    return result;
  }

  void reset(size_type bits = 0, size_type size = 0) {
    bits = checked_bits(bits);
    data_.reset(min_data_size(size, bits),
                storage_type::initialization::zero_init);
    size_ = size;
    bits_ = bits;
  }

  void resize(size_type new_size, T value = T{}) {
    auto const old_size = size_;

    if (new_size > old_size) {
      if constexpr (AutoBitWidth) {
        ensure_bits(required_bits(value), new_size);
      }

      data_.reserve(used_blocks(), min_data_size(new_size, bits_));
      fill_values(data_, bits_, old_size, new_size, value);
    }

    size_ = new_size;
  }

  void reserve(size_type size) {
    data_.reserve(used_blocks(), min_data_size(size, bits_));
  }

  void shrink_to_fit() { data_.shrink_to_fit(used_blocks()); }

  void optimize_storage()
    requires AutoBitWidth
  {
    auto const new_bits = required_bits();
    if (new_bits == bits_) {
      shrink_to_fit();
    } else {
      repack_data(new_bits, size_);
    }
  }

  void truncate_to_bits(size_type new_bits) {
    new_bits = checked_bits(new_bits);
    if (new_bits != bits_) {
      repack_data(new_bits, size_);
    }
  }

  size_type capacity() const {
    return bits_ > 0 ? (data_.capacity() * bits_per_block) / bits_ : 0;
  }

  void clear() { size_ = 0; }

  size_type size() const { return size_; }
  size_type bits() const { return bits_; }

  size_type size_in_bytes() const {
    return used_blocks() * sizeof(underlying_type);
  }

  bool empty() const { return size_ == 0; }

  T operator[](size_type i) const { return get(i); }

  T at(size_type i) const {
    if (i >= size_) {
      throw std::out_of_range("basic_packed_int_vector::at");
    }
    return get(i);
  }

  T get(size_type i) const {
    return bits_ > 0
               ? bit_view(data_.data()).template read<T>({i * bits_, bits_})
               : T{};
  }

  value_proxy operator[](size_type i) { return value_proxy{*this, i}; }

  value_proxy at(size_type i) {
    if (i >= size_) {
      throw std::out_of_range("basic_packed_int_vector::at");
    }
    return (*this)[i];
  }

  void set(size_type i, T value) {
    if constexpr (AutoBitWidth) {
      ensure_bits(required_bits(value), size_);
    }

    write_value(data_, bits_, i, value);
  }

  void push_back(T value) {
    auto const new_size = size_ + 1;

    if constexpr (AutoBitWidth) {
      ensure_bits(required_bits(value), new_size);
    }

    data_.reserve(used_blocks(), min_data_size(new_size, bits_));
    write_value(data_, bits_, size_, value);
    size_ = new_size;
  }

  void pop_back() {
    assert(size_ > 0);

    --size_;
  }

  T back() const { return get(size_ - 1); }

  value_proxy back() { return (*this)[size_ - 1]; }

  T front() const { return get(0); }

  value_proxy front() { return (*this)[0]; }

  std::vector<T> unpack() const {
    std::vector<T> result(size_);
    for (size_type i = 0; i < size_; ++i) {
      result[i] = get(i);
    }
    return result;
  }

 private:
  using storage_type = detail::block_storage<underlying_type>;

  static size_type checked_bits(size_type bits) {
    if (bits > bits_per_block) {
      throw std::invalid_argument("basic_packed_int_vector: invalid bit width");
    }
    return bits;
  }

  static constexpr size_type min_data_size(size_type size, size_type bits) {
    return bits == 0 ? 0 : (size * bits + bits_per_block - 1) / bits_per_block;
  }

  size_type used_blocks() const { return min_data_size(size_, bits_); }

  static void
  write_value(storage_type& data, size_type bits, size_type i, T value) {
    if (bits > 0) {
      bit_view(data.data()).write({i * bits, bits}, value);
    }
  }

  static void fill_values(storage_type& data, size_type bits, size_type first,
                          size_type last, T value) {
    for (size_type i = first; i < last; ++i) {
      write_value(data, bits, i, value);
    }
  }

  void ensure_bits(size_type needed_bits, size_type new_size)
    requires AutoBitWidth
  {
    auto const new_bits = std::max(bits_, needed_bits);

    if (new_bits != bits_) {
      repack_data(new_bits, new_size);
    }
  }

  void repack_data(size_type new_bits, size_type new_size) {
    auto const blocks_needed = min_data_size(new_size, new_bits);
    auto const copy_size = std::min(size_, new_size);

    // If bits_ was 0, we skip copying the old data (since it is all zeroes),
    // so we *must* zero-initialize the new storage here.
    auto const init_mode = bits_ == 0 && copy_size > 0
                               ? storage_type::initialization::zero_init
                               : storage_type::initialization::no_init;
    auto new_data = storage_type(blocks_needed, init_mode);

    if (new_bits != 0 && bits_ != 0 && copy_size > 0) {
      auto src = bit_view(data_.data());
      auto dst = bit_view(new_data.data());

      for (size_type i = 0; i < copy_size; ++i) {
        dst.write({i * new_bits, new_bits},
                  src.template read<T>({i * bits_, bits_}));
      }
    }

    data_.swap(new_data);
    bits_ = new_bits;
  }

  size_type size_{0};
  size_type bits_{0};
  storage_type data_;
};

} // namespace detail

template <std::integral T>
  requires(!std::same_as<T, bool>)
using packed_int_vector = detail::basic_packed_int_vector<T, false>;

template <std::integral T>
  requires(!std::same_as<T, bool>)
using auto_packed_int_vector = detail::basic_packed_int_vector<T, true>;

} // namespace dwarfs::internal
