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

#include <cassert>
#include <concepts>
#include <limits>
#include <span>
#include <vector>

#include <dwarfs/types.h>

namespace dwarfs {

class file_range {
 public:
  file_range() = default;

  template <std::integral T>
    requires(std::numeric_limits<T>::digits >= 31)
  file_range(file_off_t offset, T size)
      : offset_{offset}
      , size_{static_cast<file_size_t>(size)} {}

  bool empty() const noexcept { return size_ == 0; }

  file_off_t begin() const noexcept { return offset_; }

  file_off_t end() const noexcept { return offset_ + size_; }

  file_off_t offset() const noexcept { return offset_; }

  file_size_t size() const noexcept { return size_; }

  file_range subrange(file_off_t offset, file_size_t size) const {
    assert(offset >= 0);
    assert(size >= 0);
    assert(offset + size <= size_);
    return {offset_ + offset, size};
  }

  file_range subrange(file_off_t offset) const {
    assert(offset >= 0);
    return {offset_ + offset, size_ - offset};
  }

  void advance(file_size_t n) {
    assert(n <= size_);
    offset_ += n;
    size_ -= n;
  }

  friend bool
  operator==(file_range const& lhs, file_range const& rhs) noexcept {
    return lhs.offset_ == rhs.offset_ && lhs.size_ == rhs.size_;
  }

  static std::vector<file_range>
  intersect(std::span<file_range const> a, std::span<file_range const> b);

  static std::vector<file_range>
  complement(std::span<file_range const> ranges, file_size_t size);

 private:
  file_off_t offset_{0};
  file_size_t size_{0};
};

} // namespace dwarfs
