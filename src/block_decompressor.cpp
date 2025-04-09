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

#include <dwarfs/block_decompressor.h>
#include <dwarfs/decompressor_registry.h>
#include <dwarfs/fstypes.h>
#include <dwarfs/malloc_byte_buffer.h>

namespace dwarfs {

block_decompressor::block_decompressor(compression_type type,
                                       std::span<uint8_t const> data) {
  impl_ = decompressor_registry::instance().create(type, data);
}

shared_byte_buffer
block_decompressor::decompress(compression_type type,
                               std::span<uint8_t const> data) {
  block_decompressor bd(type, data);
  auto target = malloc_byte_buffer::create_reserve(bd.uncompressed_size());
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
