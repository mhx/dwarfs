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
#include <cstring>
#include <stdexcept>
#include <system_error>

#include <dwarfs/malloc_byte_buffer.h>
#include <dwarfs/os_access.h>

#include <dwarfs/internal/mappable_file.h>
#include <dwarfs/reader/internal/block_cache_byte_buffer_factory.h>

namespace dwarfs::reader::internal {

using namespace dwarfs::internal;

namespace {

class mmap_byte_buffer_impl final : public mutable_byte_buffer_interface {
 public:
  mmap_byte_buffer_impl(os_access const& os, size_t size)
      : mm_{os.map_empty(size)}
      , data_{reinterpret_cast<uint8_t*>(mm_.span<uint8_t>().data())} {}

  size_t size() const override { return size_; }

  size_t capacity() const override { return mm_.size(); }

  uint8_t const* data() const override { return data_; }

  uint8_t* mutable_data() override { return data_; }

  std::span<uint8_t const> span() const override { return {data_, size_}; }

  std::span<uint8_t> mutable_span() override { return {data_, size_}; }

  void clear() override { frozen_error("clear"); }

  void reserve(size_t size) override {
    if (size > mm_.size()) {
      frozen_error("reserve");
    }
  }

  void resize(size_t size) override {
    if (size > mm_.size()) {
      frozen_error("resize beyond capacity");
    }
    size_ = size;
  }

  void shrink_to_fit() override { frozen_error("shrink_to_fit"); }

  void freeze_location() override {
    // always frozen
  }

  void append(void const* data, size_t size) override {
    if (size_ + size > mm_.size()) {
      frozen_error("append beyond capacity");
    }
    std::memcpy(data_ + size_, data, size);
    size_ += size;
  }

  malloc_buffer& raw_buffer() override {
    throw std::runtime_error(
        "operation not allowed on mmap buffer: raw_buffer");
  }

 private:
  void frozen_error(std::string_view what) const {
    throw std::runtime_error("operation not allowed on mmap buffer: " +
                             std::string{what});
  }

  memory_mapping mm_;
  uint8_t* data_;
  size_t size_{0};
};

class block_cache_byte_buffer_factory_impl
    : public byte_buffer_factory_interface {
 public:
  block_cache_byte_buffer_factory_impl(os_access const& os,
                                       block_cache_allocation_mode mode)
      : os_{os}
      , mode_{mode} {}

  mutable_byte_buffer create_mutable_fixed_reserve(size_t size) const override {
    if (mode_ == block_cache_allocation_mode::MMAP) {
      return mutable_byte_buffer{
          std::make_shared<mmap_byte_buffer_impl>(os_, size)};
    }
    return malloc_byte_buffer::create_reserve(size);
  }

 private:
  os_access const& os_;
  block_cache_allocation_mode mode_;
};

} // namespace

byte_buffer_factory
block_cache_byte_buffer_factory::create(os_access const& os) {
  return create(os, block_cache_allocation_mode::MALLOC);
}

byte_buffer_factory
block_cache_byte_buffer_factory::create(os_access const& os,
                                        block_cache_allocation_mode mode) {
  return byte_buffer_factory{
      std::make_shared<block_cache_byte_buffer_factory_impl>(os, mode)};
}

} // namespace dwarfs::reader::internal
