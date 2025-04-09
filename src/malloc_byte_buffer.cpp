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

#include <atomic>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>

#include <dwarfs/malloc_byte_buffer.h>

#include <dwarfs/internal/malloc_buffer.h>

namespace dwarfs {

namespace {

class malloc_byte_buffer_impl : public mutable_byte_buffer_interface {
 public:
  malloc_byte_buffer_impl() = default;
  explicit malloc_byte_buffer_impl(size_t size)
      : data_(size) {}
  explicit malloc_byte_buffer_impl(std::string_view data)
      : data_{data.data(), data.size()} {}
  explicit malloc_byte_buffer_impl(std::span<uint8_t const> data)
      : data_{data} {}
  explicit malloc_byte_buffer_impl(internal::malloc_buffer&& data)
      : data_{std::move(data)} {}

  size_t size() const override { return data_.size(); }

  size_t capacity() const override { return data_.capacity(); }

  uint8_t const* data() const override { return data_.data(); }

  uint8_t* mutable_data() override { return data_.data(); }

  std::span<uint8_t const> span() const override {
    return {data_.data(), data_.size()};
  }

  std::span<uint8_t> mutable_span() override {
    return {data_.data(), data_.size()};
  }

  void clear() override {
    assert_not_frozen("clear");
    data_.clear();
  }

  void reserve(size_t size) override {
    assert_not_frozen("reserve");
    data_.reserve(size);
  }

  void resize(size_t size) override {
    if (frozen() && size > data_.capacity()) {
      frozen_error("resize beyond capacity");
    }
    data_.resize(size);
  }

  void shrink_to_fit() override {
    assert_not_frozen("shrink_to_fit");
    data_.shrink_to_fit();
  }

  void freeze_location() override {
    frozen_.store(true, std::memory_order_release);
  }

  void append(void const* data, size_t size) override {
    if (frozen() && data_.size() + size > data_.capacity()) {
      frozen_error("append beyond capacity");
    }
    data_.append(data, size);
  }

  internal::malloc_buffer& raw_buffer() override { return data_; }

 private:
  void assert_not_frozen(std::string_view what) const {
    if (frozen()) {
      frozen_error(what);
    }
  }

  void frozen_error(std::string_view what) const {
    throw std::runtime_error("operation not allowed on frozen buffer: " +
                             std::string{what});
  }

  bool frozen() const { return frozen_.load(std::memory_order_acquire); }

  internal::malloc_buffer data_;
  std::atomic<bool> frozen_{false};
  static_assert(std::atomic<bool>::is_always_lock_free);
};

} // namespace

mutable_byte_buffer malloc_byte_buffer::create() {
  return mutable_byte_buffer{std::make_shared<malloc_byte_buffer_impl>()};
}

mutable_byte_buffer malloc_byte_buffer::create(size_t size) {
  return mutable_byte_buffer{std::make_shared<malloc_byte_buffer_impl>(size)};
}

mutable_byte_buffer malloc_byte_buffer::create_zeroed(size_t size) {
  auto buffer = std::make_shared<malloc_byte_buffer_impl>(size);
  std::memset(buffer->mutable_data(), 0, size);
  return mutable_byte_buffer{std::move(buffer)};
}

mutable_byte_buffer malloc_byte_buffer::create_reserve(size_t size) {
  auto rv = std::make_shared<malloc_byte_buffer_impl>();
  rv->reserve(size);
  return mutable_byte_buffer{std::move(rv)};
}

mutable_byte_buffer malloc_byte_buffer::create(std::string_view data) {
  return mutable_byte_buffer{std::make_shared<malloc_byte_buffer_impl>(data)};
}

mutable_byte_buffer malloc_byte_buffer::create(std::span<uint8_t const> data) {
  return mutable_byte_buffer{std::make_shared<malloc_byte_buffer_impl>(data)};
}

mutable_byte_buffer malloc_byte_buffer::create(internal::malloc_buffer&& data) {
  return mutable_byte_buffer{
      std::make_shared<malloc_byte_buffer_impl>(std::move(data))};
}

} // namespace dwarfs
