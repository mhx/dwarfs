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

#include <dwarfs/block_compressor.h>
#include <dwarfs/compressor_factory.h>
#include <dwarfs/config.h>
#include <dwarfs/container/sorted_array_map.h>
#include <dwarfs/error.h>
#include <dwarfs/library_dependencies.h>
#include <dwarfs/option_map.h>

namespace dwarfs {

namespace {

constexpr container::sorted_array_map compressors{
    std::pair{compression_type::NONE,
              &detail::compressor_factory_for<compression_type::NONE>},
#ifdef DWARFS_HAVE_LIBLZMA
    std::pair{compression_type::LZMA,
              &detail::compressor_factory_for<compression_type::LZMA>},
#endif
#ifdef DWARFS_HAVE_LIBZSTD
    std::pair{compression_type::ZSTD,
              &detail::compressor_factory_for<compression_type::ZSTD>},
#endif
#ifdef DWARFS_HAVE_LIBLZ4
    std::pair{compression_type::LZ4,
              &detail::compressor_factory_for<compression_type::LZ4>},
#endif
#ifdef DWARFS_HAVE_LIBLZ4
    std::pair{compression_type::LZ4HC,
              &detail::compressor_factory_for<compression_type::LZ4HC>},
#endif
#ifdef DWARFS_HAVE_LIBBROTLI
    std::pair{compression_type::BROTLI,
              &detail::compressor_factory_for<compression_type::BROTLI>},
#endif
#ifdef DWARFS_HAVE_FLAC
    std::pair{compression_type::FLAC,
              &detail::compressor_factory_for<compression_type::FLAC>},
#endif
#ifdef DWARFS_HAVE_RICEPP
    std::pair{compression_type::RICEPP,
              &detail::compressor_factory_for<compression_type::RICEPP>},
#endif
#ifdef DWARFS_HAVE_LIBZXC
    std::pair{compression_type::ZXC,
              &detail::compressor_factory_for<compression_type::ZXC>},
#endif
};

} // namespace

compressor_factory const& get_compressor_factory(compression_type type) {
  if (auto factory = compressors.get(type)) {
    return (*factory)();
  }

  throw unsupported_compression_error(type);
}

void for_each_compressor(
    std::function<void(compression_type, compressor_info const&)> const& fn) {
  for (auto const& [type, get_factory] : compressors) {
    fn(type, get_factory());
  }
}

void add_compressor_library_dependencies(library_dependencies& deps) {
  for (auto const& entry : compressors) {
    for (auto const& lib : entry.second().library_dependencies()) {
      deps.add_library(lib);
    }
  }
}

block_compressor::block_compressor(std::string const& spec) {
  option_map om(spec);

  for (auto const& entry : compressors) {
    auto const& factory = entry.second();

    if (factory.name() == om.choice()) {
      impl_ = factory.create(om);
      om.report();
      return;
    }
  }

  DWARFS_THROW(runtime_error, "unknown compression: " + om.choice());
}

} // namespace dwarfs
