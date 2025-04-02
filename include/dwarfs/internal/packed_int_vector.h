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

#include <cstdint>
#include <limits>
#include <vector>

#include <folly/lang/BitsClass.h>

namespace dwarfs::internal {

template <typename T>
class packed_int_vector {
 public:
  using value_type = T;
  using bits_type = folly::Bits<T>;
  using underlying_type = typename bits_type::UnderlyingType;
  using size_type = size_t;

  static constexpr size_type bits_per_block{bits_type::bitsPerBlock};

  class value_proxy {
   public:
    value_proxy(packed_int_vector& vec, size_type i)
        : vec_{vec}
        , i_{i} {}

    operator T() const { return vec_.get(i_); }

    value_proxy& operator=(T value) {
      vec_.set(i_, value);
      return *this;
    }

   private:
    packed_int_vector& vec_;
    size_type i_;
  };

  packed_int_vector() = default;
  packed_int_vector(size_type bits)
      : bits_{bits} {}
  packed_int_vector(size_type bits, size_type size)
      : size_{size}
      , bits_{bits}
      , data_{min_data_size(size, bits)} {}

  packed_int_vector(packed_int_vector const&) = default;
  packed_int_vector(packed_int_vector&&) = default;
  packed_int_vector& operator=(packed_int_vector const&) = default;
  packed_int_vector& operator=(packed_int_vector&&) = default;

  void reset(size_type bits = 0, size_type size = 0) {
    size_ = size;
    bits_ = bits;
    data_.clear();
    data_.resize(min_data_size(size, bits));
  }

  void resize(size_type size) {
    size_ = size;
    data_.resize(min_data_size(size, bits_));
  }

  void reserve(size_type size) { data_.reserve(min_data_size(size, bits_)); }

  void shrink_to_fit() { data_.shrink_to_fit(); }

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

  T operator[](size_type i) const { return this->get(i); }

  T at(size_type i) const {
    if (i >= size_) {
      throw std::out_of_range("packed_int_vector::at");
    }
    return this->get(i);
  }

  T get(size_type i) const {
    return bits_ > 0 ? bits_type::get(data_.data(), i * bits_, bits_) : 0;
  }

  value_proxy operator[](size_type i) { return value_proxy{*this, i}; }

  value_proxy at(size_type i) {
    if (i >= size_) {
      throw std::out_of_range("packed_int_vector::at");
    }
    return this->operator[](i);
  }

  void set(size_type i, T value) {
    if (bits_ > 0) {
      bits_type::set(data_.data(), i * bits_, bits_, value);
    }
  }

  void push_back(T value) {
    if (min_data_size(size_ + 1, bits_) > data_.size()) {
      data_.resize(data_.size() + 1);
    }
    set(size_++, value);
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

  value_proxy back() { return this->operator[](size_ - 1); }

  T front() const { return get(0); }

  value_proxy front() { return this->operator[](0); }

  std::vector<T> unpack() const {
    std::vector<T> result(size_);
    for (size_type i = 0; i < size_; ++i) {
      result[i] = get(i);
    }
    return result;
  }

 private:
  static constexpr size_type min_data_size(size_type size, size_type bits) {
    return (size * bits + bits_per_block - 1) / bits_per_block;
  }

  size_type size_{0};
  size_type bits_{0};
  std::vector<underlying_type> data_;
};

} // namespace dwarfs::internal
