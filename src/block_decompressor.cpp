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

#include <dwarfs/block_decompressor.h>
#include <dwarfs/decompressor_registry.h>
#include <dwarfs/fstypes.h>
#include <dwarfs/vector_byte_buffer.h>

namespace dwarfs {

block_decompressor::block_decompressor(compression_type type,
                                       std::span<uint8_t const> data) {
  impl_ = decompressor_registry::instance().create(type, data);
}

shared_byte_buffer
block_decompressor::decompress(compression_type type,
                               std::span<uint8_t const> data) {
  block_decompressor bd(type, data);
  auto target = vector_byte_buffer::create_reserve(bd.uncompressed_size());
  bd.start_decompression(target);
  bd.decompress_frame(bd.uncompressed_size());
  return target.share();
}

shared_byte_buffer
block_decompressor::start_decompression(mutable_byte_buffer target) {
  impl_->start_decompression(target);
  target.freeze_location();
  return target.share();
}

shared_byte_buffer
block_decompressor::start_decompression(byte_buffer_factory const& bbf) {
  auto target = bbf.create_mutable_fixed_reserve(impl_->uncompressed_size());
  return start_decompression(target);
}

} // namespace dwarfs
