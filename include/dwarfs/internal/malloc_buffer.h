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
#include <cstring>
#include <span>

namespace dwarfs::internal {

class malloc_buffer {
 public:
  using value_type = uint8_t;

  malloc_buffer() = default;
  malloc_buffer(size_t size);
  malloc_buffer(void const* data, size_t size);
  malloc_buffer(std::span<value_type const> data);

  ~malloc_buffer();

  malloc_buffer(malloc_buffer&&) = default;
  malloc_buffer& operator=(malloc_buffer&&) = default;

  bool empty() const { return size_ == 0; }

  size_t size() const { return size_; }
  size_t capacity() const { return capacity_; }

  value_type const* data() const { return data_; }
  value_type* data() { return data_; }

  void append(void const* data, size_t size) {
    reserve(size_ + size);
    copy(data_ + size_, data, size);
    size_ += size;
  }

  void clear() { size_ = 0; }

  void resize(size_t new_size) {
    reserve(new_size);
    size_ = new_size;
  }

  void reserve(size_t new_capacity) {
    if (new_capacity > capacity_) {
      grow(new_capacity);
    }
  }

  void shrink_to_fit();

 private:
  void grow(size_t new_size);
  void resize_buffer(size_t new_size);
  static void copy(void* dest, void const* src, size_t size) {
    // TODO: try std::copy or even something custom
    std::memcpy(dest, src, size);
  }

  value_type* data_{nullptr};
  size_t size_{0};
  size_t capacity_{0};
};

} // namespace dwarfs::internal
