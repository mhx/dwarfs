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
#include <concepts>
#include <cstddef>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include <dwarfs/internal/packed_int_vector.h>

namespace dwarfs::internal {

template <std::integral T, std::size_t SegmentElements = 1024>
  requires(!std::same_as<T, bool>)
class segmented_packed_int_vector {
  static_assert(SegmentElements > 0);

 public:
  using value_type = T;
  using size_type = std::size_t;
  using segment_type = auto_packed_int_vector<T>;

  static constexpr size_type segment_elements = SegmentElements;
  static constexpr size_type bits_per_block = segment_type::bits_per_block;

  class value_proxy {
   public:
    value_proxy(segmented_packed_int_vector& vec, size_type i)
        : vec_{vec}
        , i_{i} {}

    operator T() const { return vec_.get(i_); }

    value_proxy& operator=(T value) {
      vec_.set(i_, value);
      return *this;
    }

   private:
    segmented_packed_int_vector& vec_;
    size_type i_;
  };

  segmented_packed_int_vector() = default;

  explicit segmented_packed_int_vector(size_type size) { resize(size); }

  segmented_packed_int_vector(size_type size, T value) { resize(size, value); }

  segmented_packed_int_vector(segmented_packed_int_vector const&) = default;
  segmented_packed_int_vector(segmented_packed_int_vector&&) = default;
  segmented_packed_int_vector&
  operator=(segmented_packed_int_vector const&) = default;
  segmented_packed_int_vector&
  operator=(segmented_packed_int_vector&&) = default;

  [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
  [[nodiscard]] size_type size() const noexcept { return size_; }
  [[nodiscard]] size_type segment_count() const noexcept {
    return segments_.size();
  }

  [[nodiscard]] size_type size_in_bytes() const {
    size_type total = 0;
    for (auto const& seg : segments_) {
      total += seg.size_in_bytes();
    }
    return total;
  }

  [[nodiscard]] std::array<size_type, bits_per_block + 1>
  segment_bits_histogram() const {
    std::array<size_type, bits_per_block + 1> hist{};
    for (auto const& seg : segments_) {
      ++hist[seg.bits()];
    }
    return hist;
  }

  void clear() {
    segments_.clear();
    size_ = 0;
  }

  T operator[](size_type i) const { return get(i); }

  value_proxy operator[](size_type i) { return value_proxy{*this, i}; }

  T at(size_type i) const {
    if (i >= size_) {
      throw std::out_of_range("segmented_packed_int_vector::at");
    }
    return get(i);
  }

  value_proxy at(size_type i) {
    if (i >= size_) {
      throw std::out_of_range("segmented_packed_int_vector::at");
    }
    return (*this)[i];
  }

  T front() const { return get(0); }

  value_proxy front() { return (*this)[0]; }

  T back() const { return get(size_ - 1); }

  value_proxy back() { return (*this)[size_ - 1]; }

  T get(size_type i) const {
    auto const [seg, off] = locate(i);
    return segments_[seg][off];
  }

  void set(size_type i, T value) {
    auto const [seg, off] = locate(i);
    segments_[seg][off] = value;
  }

  void push_back(T value) {
    if (segments_.empty() || segments_.back().size() == segment_elements) {
      segments_.push_back(make_empty_segment());
    }

    segments_.back().push_back(value);
    ++size_;
  }

  void pop_back() {
    assert(size_ > 0);

    segments_.back().pop_back();
    --size_;

    if (segments_.back().empty()) {
      segments_.pop_back();
    }
  }

  void resize(size_type new_size, T value = T{}) {
    if (new_size < size_) {
      shrink_to(new_size);
    } else if (new_size > size_) {
      grow_to(new_size, value);
    }
  }

  void optimize_storage() {
    for (auto& seg : segments_) {
      seg.optimize_storage();
    }
    segments_.shrink_to_fit();
  }

  [[nodiscard]] std::vector<T> unpack() const {
    std::vector<T> result(size_);
    for (size_type i = 0; i < size_; ++i) {
      result[i] = get(i);
    }
    return result;
  }

 private:
  [[nodiscard]] static constexpr std::pair<size_type, size_type>
  locate(size_type i) noexcept {
    return {i / segment_elements, i % segment_elements};
  }

  [[nodiscard]] static segment_type make_empty_segment() {
    segment_type seg(0);
    seg.reserve(segment_elements);
    return seg;
  }

  void shrink_to(size_type new_size) {
    if (new_size == 0) {
      clear();
      return;
    }

    auto const new_segment_count =
        (new_size + segment_elements - 1) / segment_elements;
    auto const last_segment_size =
        new_size - (new_segment_count - 1) * segment_elements;

    segments_.resize(new_segment_count);
    segments_.back().resize(last_segment_size);
    size_ = new_size;
  }

  void grow_to(size_type new_size, T value) {
    while (size_ < new_size) {
      if (segments_.empty() || segments_.back().size() == segment_elements) {
        segments_.push_back(make_empty_segment());
      }

      auto& seg = segments_.back();
      auto const available = segment_elements - seg.size();
      auto const remaining = new_size - size_;
      auto const n = available < remaining ? available : remaining;

      seg.resize(seg.size() + n, value);
      size_ += n;
    }
  }

  size_type size_{0};
  std::vector<segment_type> segments_;
};

} // namespace dwarfs::internal
