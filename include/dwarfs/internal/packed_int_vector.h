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
      , data_(min_data_size(size, bits_)) {}

  basic_packed_int_vector(basic_packed_int_vector const&) = default;
  basic_packed_int_vector(basic_packed_int_vector&&) = default;
  basic_packed_int_vector& operator=(basic_packed_int_vector const&) = default;
  basic_packed_int_vector& operator=(basic_packed_int_vector&&) = default;

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
    std::vector<underlying_type> new_data(min_data_size(size, bits));
    data_.swap(new_data);
    size_ = size;
    bits_ = bits;
  }

  void resize(size_type new_size, T value = T{}) {
    auto const old_size = size_;

    if constexpr (AutoBitWidth) {
      if (new_size > old_size) {
        ensure_bits(required_bits(value), new_size);
      }
    }

    data_.resize(min_data_size(new_size, bits_));
    fill_values(data_, bits_, old_size, new_size, value);
    size_ = new_size;
  }

  void reserve(size_type size) { data_.reserve(min_data_size(size, bits_)); }

  void shrink_to_fit() { data_.shrink_to_fit(); }

  void optimize_storage()
    requires AutoBitWidth
  {
    auto const new_bits = required_bits();
    if (new_bits == bits_) {
      data_.shrink_to_fit();
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

  void clear() {
    size_ = 0;
    data_.clear();
  }

  size_type size() const { return size_; }
  size_type bits() const { return bits_; }

  size_type size_in_bytes() const {
    return data_.size() * sizeof(underlying_type);
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
    if constexpr (AutoBitWidth) {
      ensure_bits(required_bits(value), size_ + 1);
    }

    auto const new_size = size_ + 1;
    data_.resize(min_data_size(new_size, bits_));
    write_value(data_, bits_, size_, value);
    size_ = new_size;
  }

  void pop_back() {
    if (size_ > 0) {
      --size_;
    }
    if (min_data_size(size_, bits_) < data_.size()) {
      data_.resize(data_.size() - 1);
    }
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
  static size_type checked_bits(size_type bits) {
    if (bits > bits_per_block) {
      throw std::invalid_argument("basic_packed_int_vector: invalid bit width");
    }
    return bits;
  }

  static constexpr size_type min_data_size(size_type size, size_type bits) {
    return bits == 0 ? 0 : (size * bits + bits_per_block - 1) / bits_per_block;
  }

  static void write_value(std::vector<underlying_type>& data, size_type bits,
                          size_type i, T value) {
    if (bits > 0) {
      bit_view(data.data()).write({i * bits, bits}, value);
    }
  }

  static void fill_values(std::vector<underlying_type>& data, size_type bits,
                          size_type first, size_type last, T value) {
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
    std::vector<underlying_type> new_data(min_data_size(new_size, new_bits));

    if (new_bits != 0 && bits_ != 0) {
      auto const copy_size = std::min(size_, new_size);

      if (copy_size > 0) {
        auto src = bit_view(data_.data());
        auto dst = bit_view(new_data.data());

        for (size_type i = 0; i < copy_size; ++i) {
          dst.write({i * new_bits, new_bits},
                    src.template read<T>({i * bits_, bits_}));
        }
      }
    }

    data_.swap(new_data);
    bits_ = new_bits;
  }

  size_type size_{0};
  size_type bits_{0};
  std::vector<underlying_type> data_;
};

} // namespace detail

template <std::integral T>
  requires(!std::same_as<T, bool>)
using packed_int_vector = detail::basic_packed_int_vector<T, false>;

template <std::integral T>
  requires(!std::same_as<T, bool>)
using auto_packed_int_vector = detail::basic_packed_int_vector<T, true>;

} // namespace dwarfs::internal
