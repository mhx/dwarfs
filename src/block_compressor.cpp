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

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>

#include <range/v3/algorithm/sort.hpp>
#include <range/v3/range/conversion.hpp>
#include <range/v3/view/map.hpp>

#include <dwarfs/block_compressor.h>
#include <dwarfs/config.h>
#include <dwarfs/error.h>
#include <dwarfs/fstypes.h>
#include <dwarfs/option_map.h>
#include <dwarfs/vector_byte_buffer.h>

namespace dwarfs {

block_compressor::block_compressor(std::string const& spec) {
  impl_ = compression_registry::instance().make_compressor(spec);
}

block_decompressor::block_decompressor(compression_type type,
                                       std::span<uint8_t const> data) {
  impl_ = compression_registry::instance().make_decompressor(type, data);
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

compression_registry& compression_registry::instance() {
  static compression_registry the_instance;
  return the_instance;
}

void compression_registry::register_factory(
    compression_type type,
    std::unique_ptr<compression_factory const>&& factory) {
  auto name = factory->name();

  if (!factories_.emplace(type, std::move(factory)).second) {
    std::cerr << "compression factory type conflict (" << name << ", "
              << static_cast<int>(type) << ")\n";
    ::abort();
  }

  if (!names_.emplace(name, type).second) {
    std::cerr << "compression factory name conflict (" << name << ", "
              << static_cast<int>(type) << ")\n";
    ::abort();
  }
}

std::unique_ptr<block_compressor::impl>
compression_registry::make_compressor(std::string_view spec) const {
  option_map om(spec);
  auto nit = names_.find(om.choice());

  if (nit == names_.end()) {
    DWARFS_THROW(runtime_error, "unknown compression: " + om.choice());
  }

  auto fit = factories_.find(nit->second);

  assert(fit != factories_.end());

  auto obj = fit->second->make_compressor(om);

  om.report();

  return obj;
}

std::unique_ptr<block_decompressor::impl>
compression_registry::make_decompressor(compression_type type,
                                        std::span<uint8_t const> data) const {
  auto fit = factories_.find(type);

  if (fit == factories_.end()) {
    DWARFS_THROW(runtime_error,
                 "unsupported compression type: " + get_compression_name(type));
  }

  return fit->second->make_decompressor(data);
}

void compression_registry::for_each_algorithm(
    std::function<void(compression_type, compression_info const&)> const& fn)
    const {
  auto types = factories_ | ranges::views::keys | ranges::to<std::vector>;

  ranges::sort(types);

  for (auto type : types) {
    fn(type, *factories_.at(type));
  }
}

compression_registry::compression_registry() {
  using namespace ::dwarfs::detail;
  using enum compression_type;

  compression_factory_registrar<NONE>::reg(*this);
#ifdef DWARFS_HAVE_LIBBROTLI
  compression_factory_registrar<BROTLI>::reg(*this);
#endif
#ifdef DWARFS_HAVE_FLAC
  compression_factory_registrar<FLAC>::reg(*this);
#endif
#ifdef DWARFS_HAVE_LIBLZ4
  compression_factory_registrar<LZ4>::reg(*this);
  compression_factory_registrar<LZ4HC>::reg(*this);
#endif
#ifdef DWARFS_HAVE_LIBLZMA
  compression_factory_registrar<LZMA>::reg(*this);
#endif
#ifdef DWARFS_HAVE_RICEPP
  compression_factory_registrar<RICEPP>::reg(*this);
#endif
#ifdef DWARFS_HAVE_LIBZSTD
  compression_factory_registrar<ZSTD>::reg(*this);
#endif
}

compression_registry::~compression_registry() = default;

} // namespace dwarfs
