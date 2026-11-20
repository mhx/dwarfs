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
#include <dwarfs/config.h>
#include <dwarfs/container/sorted_array_map.h>
#include <dwarfs/decompressor_factory.h>
#include <dwarfs/fstypes.h>
#include <dwarfs/library_dependencies.h>
#include <dwarfs/malloc_byte_buffer.h>

namespace dwarfs {

namespace {

constexpr container::sorted_array_map decompressors{
    std::pair{compression_type::NONE,
              &detail::decompressor_factory_for<compression_type::NONE>},
#ifdef DWARFS_HAVE_LIBLZMA
    std::pair{compression_type::LZMA,
              &detail::decompressor_factory_for<compression_type::LZMA>},
#endif
#ifdef DWARFS_HAVE_LIBZSTD
    std::pair{compression_type::ZSTD,
              &detail::decompressor_factory_for<compression_type::ZSTD>},
#endif
#ifdef DWARFS_HAVE_LIBLZ4
    std::pair{compression_type::LZ4,
              &detail::decompressor_factory_for<compression_type::LZ4>},
#endif
#ifdef DWARFS_HAVE_LIBLZ4
    std::pair{compression_type::LZ4HC,
              &detail::decompressor_factory_for<compression_type::LZ4HC>},
#endif
#ifdef DWARFS_HAVE_LIBBROTLI
    std::pair{compression_type::BROTLI,
              &detail::decompressor_factory_for<compression_type::BROTLI>},
#endif
#ifdef DWARFS_HAVE_FLAC
    std::pair{compression_type::FLAC,
              &detail::decompressor_factory_for<compression_type::FLAC>},
#endif
#ifdef DWARFS_HAVE_RICEPP
    std::pair{compression_type::RICEPP,
              &detail::decompressor_factory_for<compression_type::RICEPP>},
#endif
#ifdef DWARFS_HAVE_LIBZXC
    std::pair{compression_type::ZXC,
              &detail::decompressor_factory_for<compression_type::ZXC>},
#endif
};

} // namespace

decompressor_factory const& get_decompressor_factory(compression_type type) {
  if (auto factory = decompressors.get(type)) {
    return (*factory)();
  }

  throw unsupported_compression_error(type);
}

void add_decompressor_library_dependencies(library_dependencies& deps) {
  for (auto const& entry : decompressors) {
    for (auto const& lib : entry.second().library_dependencies()) {
      deps.add_library(lib);
    }
  }
}

block_decompressor::block_decompressor(compression_type type,
                                       std::span<uint8_t const> data) {
  impl_ = get_decompressor_factory(type).create(data);
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
