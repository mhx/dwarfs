/* vim:set ts=2 sw=2 sts=2 et: */
/**
 * \author     Marcus Holland-Moritz (github@mhxnet.de)
 * \copyright  Copyright (c) Marcus Holland-Moritz
 *
 * This file is part of dwarfs.
 *
 * dwarfs is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * dwarfs is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with dwarfs.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <cassert>
#include <stdexcept>

#ifdef _WIN32
#include <dwarfs/vector_byte_buffer.h>
#else
#include <sys/mman.h>
#endif

#include <dwarfs/reader/block_cache_byte_buffer_factory.h>

namespace dwarfs::reader {

namespace {

#ifndef _WIN32
class mmap_file {
 public:
  mmap_file(size_t size)
      : data_{::mmap(nullptr, size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)}
      , size_{size} {
    if (data_ == MAP_FAILED) {
      throw std::runtime_error("mmap failed");
    }
  }

  size_t size() const { return size_; }

  uint8_t* data() { return static_cast<uint8_t*>(data_); }
  uint8_t const* data() const { return static_cast<uint8_t const*>(data_); }

  ~mmap_file() {
    auto rv [[maybe_unused]] = ::munmap(data_, size_);
    assert(rv == 0);
  }

 private:
  void* data_;
  size_t size_;
};

class mmap_byte_buffer_impl : public mutable_byte_buffer_interface {
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

  std::vector<uint8_t>& raw_vector() override {
    throw std::runtime_error(
        "operation not allowed on mmap buffer: raw_vector");
  }

 private:
  void frozen_error(std::string_view what) const {
    throw std::runtime_error("operation not allowed on mmap buffer: " +
                             std::string{what});
  }

  mmap_file data_;
  size_t size_{0};
};
#endif

class block_cache_byte_buffer_factory_impl
    : public byte_buffer_factory_interface {
 public:
  mutable_byte_buffer create_mutable_fixed_reserve(size_t size) const override {
#ifdef _WIN32
    return vector_byte_buffer::create_reserve(size);
#else
    return mutable_byte_buffer{std::make_shared<mmap_byte_buffer_impl>(size)};
#endif
  }
};

} // namespace

byte_buffer_factory block_cache_byte_buffer_factory::create() {
  return byte_buffer_factory{
      std::make_shared<block_cache_byte_buffer_factory_impl>()};
}

} // namespace dwarfs::reader
