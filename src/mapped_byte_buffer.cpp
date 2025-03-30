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

#include <dwarfs/mapped_byte_buffer.h>

namespace dwarfs {

namespace {

class mapped_byte_buffer_impl : public byte_buffer_interface {
 public:
  mapped_byte_buffer_impl(std::span<uint8_t const> data,
                          std::shared_ptr<mmif const> mm)
      : data_{data}
      , mm_{std::move(mm)} {}

  size_t size() const override { return data_.size(); }

  size_t capacity() const override { return data_.size(); }

  uint8_t const* data() const override { return data_.data(); }

  std::span<uint8_t const> span() const override {
    return {data_.data(), data_.size()};
  }

 private:
  std::span<uint8_t const> data_;
  std::shared_ptr<mmif const> mm_;
};

} // namespace

shared_byte_buffer mapped_byte_buffer::create(std::span<uint8_t const> data,
                                              std::shared_ptr<mmif const> mm) {
  return shared_byte_buffer{
      std::make_shared<mapped_byte_buffer_impl>(data, std::move(mm))};
}

} // namespace dwarfs
