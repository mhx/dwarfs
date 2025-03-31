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

#include <dwarfs/vector_byte_buffer_factory.h>

namespace dwarfs {

namespace {

class vector_byte_buffer_factory_impl : public byte_buffer_factory_interface {
 public:
  mutable_byte_buffer create_mutable_fixed_reserve(size_t size) const override {
    return vector_byte_buffer::create_reserve(size);
  }
};

} // namespace

byte_buffer_factory vector_byte_buffer_factory::create() {
  return byte_buffer_factory{
      std::make_shared<vector_byte_buffer_factory_impl>()};
}

} // namespace dwarfs
