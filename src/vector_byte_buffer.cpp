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

#include <vector>

#include <dwarfs/vector_byte_buffer.h>

namespace dwarfs {

namespace {

class vector_byte_buffer_impl : public mutable_byte_buffer_interface {
 public:
  vector_byte_buffer_impl() = default;
  explicit vector_byte_buffer_impl(size_t size)
      : data_(size) {}
  explicit vector_byte_buffer_impl(std::string_view data)
      : data_{data.begin(), data.end()} {}
  explicit vector_byte_buffer_impl(std::span<uint8_t const> data)
      : data_{data.begin(), data.end()} {}
  explicit vector_byte_buffer_impl(std::vector<uint8_t>&& data)
      : data_{std::move(data)} {}

  size_t size() const override { return data_.size(); }

  uint8_t const* data() const override { return data_.data(); }

  uint8_t* mutable_data() override { return data_.data(); }

  std::span<uint8_t const> span() const override {
    return {data_.data(), data_.size()};
  }

  std::span<uint8_t> mutable_span() override {
    return {data_.data(), data_.size()};
  }

  void clear() override { data_.clear(); }

  void reserve(size_t size) override { data_.reserve(size); }

  void resize(size_t size) override { data_.resize(size); }

  void shrink_to_fit() override { data_.shrink_to_fit(); }

  std::vector<uint8_t>& raw_vector() override { return data_; }

 private:
  std::vector<uint8_t> data_;
};

} // namespace

mutable_byte_buffer vector_byte_buffer::create() {
  return mutable_byte_buffer{std::make_shared<vector_byte_buffer_impl>()};
}

mutable_byte_buffer vector_byte_buffer::create(size_t size) {
  return mutable_byte_buffer{std::make_shared<vector_byte_buffer_impl>(size)};
}

mutable_byte_buffer vector_byte_buffer::create(std::string_view data) {
  return mutable_byte_buffer{std::make_shared<vector_byte_buffer_impl>(data)};
}

mutable_byte_buffer vector_byte_buffer::create(std::span<uint8_t const> data) {
  return mutable_byte_buffer{std::make_shared<vector_byte_buffer_impl>(data)};
}

mutable_byte_buffer vector_byte_buffer::create(std::vector<uint8_t>&& data) {
  return mutable_byte_buffer{
      std::make_shared<vector_byte_buffer_impl>(std::move(data))};
}

} // namespace dwarfs
