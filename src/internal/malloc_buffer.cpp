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

#include <cassert>
#include <cstdlib>
#include <new>

#include <dwarfs/internal/malloc_buffer.h>

namespace dwarfs::internal {

malloc_buffer::malloc_buffer(size_t size)
    // NOLINTNEXTLINE(cppcoreguidelines-no-malloc)
    : data_{static_cast<value_type*>(std::malloc(size))}
    , size_{size}
    , capacity_{size} {
  if (!data_) {
    throw std::bad_alloc();
  }
}

malloc_buffer::malloc_buffer(void const* data, size_t size)
    : malloc_buffer(size) {
  copy(data_, data, size);
}

malloc_buffer::malloc_buffer(std::span<value_type const> data)
    : malloc_buffer(data.size_bytes()) {
  copy(data_, data.data(), data.size_bytes());
}

malloc_buffer::~malloc_buffer() {
  if (data_) {
    // NOLINTNEXTLINE(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)
    std::free(data_);
  }
}

void malloc_buffer::resize_buffer(size_t new_size) {
  // NOLINTNEXTLINE(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)
  auto new_data = static_cast<value_type*>(std::realloc(data_, new_size));
  if (!new_data) {
    throw std::bad_alloc();
  }
  data_ = new_data;
  capacity_ = new_size;
}

void malloc_buffer::grow(size_t new_size) {
  assert(new_size > size_);
  resize_buffer(new_size);
}

void malloc_buffer::shrink_to_fit() {
  if (size_ < capacity_) {
    resize_buffer(size_);
  }
}

} // namespace dwarfs::internal
