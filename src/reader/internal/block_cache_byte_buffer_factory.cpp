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

#ifdef _WIN32
#include <folly/portability/Windows.h>
#else
#include <sys/mman.h>
#endif

#include <dwarfs/malloc_byte_buffer.h>

#include <dwarfs/reader/internal/block_cache_byte_buffer_factory.h>

namespace dwarfs::reader::internal {

namespace {

class mmap_block {
 public:
  explicit mmap_block(size_t size)
      : data_{allocate(size)}
      , size_{size} {}

  ~mmap_block() {
    if (data_) {
      deallocate(data_, size_);
    }
  }

  mmap_block(mmap_block const& other) = delete;
  mmap_block& operator=(mmap_block const& other) = delete;
  mmap_block(mmap_block&& other) = delete;
  mmap_block& operator=(mmap_block&& other) = delete;

  size_t size() const { return size_; }

  uint8_t* data() { return static_cast<uint8_t*>(data_); }
  uint8_t const* data() const { return static_cast<uint8_t const*>(data_); }

 private:
  static void* allocate(size_t size) {
#ifdef _WIN32
    void* data =
        ::VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!data) {
      std::error_code ec(::GetLastError(), std::system_category());
      throw std::runtime_error("VirtualAlloc failed: " + ec.message());
    }
#else
    void* data = ::mmap(nullptr, size, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (data == MAP_FAILED) {
      throw std::runtime_error("mmap failed");
    }
#endif
    return data;
  }

  static void deallocate(void* data, size_t size) {
#ifdef _WIN32
    auto rv [[maybe_unused]] = ::VirtualFree(data, 0, MEM_RELEASE);
    assert(rv);
#else
    auto rv [[maybe_unused]] = ::munmap(data, size);
    assert(rv == 0);
#endif
  }

  void* data_;
  size_t size_;
};

class mmap_byte_buffer_impl final : public mutable_byte_buffer_interface {
 public:
  explicit mmap_byte_buffer_impl(size_t size)
      : data_{size} {}

  size_t size() const override { return size_; }

  size_t capacity() const override { return data_.size(); }

  uint8_t const* data() const override { return data_.data(); }

  uint8_t* mutable_data() override { return data_.data(); }

  std::span<uint8_t const> span() const override {
    return {data_.data(), size_};
  }

  std::span<uint8_t> mutable_span() override { return {data_.data(), size_}; }

  void clear() override { frozen_error("clear"); }

  void reserve(size_t size) override {
    if (size > data_.size()) {
      frozen_error("reserve");
    }
  }

  void resize(size_t size) override {
    if (size > data_.size()) {
      frozen_error("resize beyond capacity");
    }
    size_ = size;
  }

  void shrink_to_fit() override { frozen_error("shrink_to_fit"); }

  void freeze_location() override {
    // always frozen
  }

  void append(void const* data, size_t size) override {
    if (size_ + size > data_.size()) {
      frozen_error("append beyond capacity");
    }
    std::memcpy(data_.data() + size_, data, size);
    size_ += size;
  }

  dwarfs::internal::malloc_buffer& raw_buffer() override {
    throw std::runtime_error(
        "operation not allowed on mmap buffer: raw_buffer");
  }

 private:
  void frozen_error(std::string_view what) const {
    throw std::runtime_error("operation not allowed on mmap buffer: " +
                             std::string{what});
  }

  mmap_block data_;
  size_t size_{0};
};

class block_cache_byte_buffer_factory_impl
    : public byte_buffer_factory_interface {
 public:
  explicit block_cache_byte_buffer_factory_impl(
      block_cache_allocation_mode mode)
      : mode_{mode} {}

  mutable_byte_buffer create_mutable_fixed_reserve(size_t size) const override {
    if (mode_ == block_cache_allocation_mode::MMAP) {
      return mutable_byte_buffer{std::make_shared<mmap_byte_buffer_impl>(size)};
    }
    return malloc_byte_buffer::create_reserve(size);
  }

 private:
  block_cache_allocation_mode mode_;
};

} // namespace

byte_buffer_factory block_cache_byte_buffer_factory::create() {
  return byte_buffer_factory{
      std::make_shared<block_cache_byte_buffer_factory_impl>(
          block_cache_allocation_mode::MALLOC)};
}

byte_buffer_factory
block_cache_byte_buffer_factory::create(block_cache_allocation_mode mode) {
  return byte_buffer_factory{
      std::make_shared<block_cache_byte_buffer_factory_impl>(mode)};
}

} // namespace dwarfs::reader::internal
