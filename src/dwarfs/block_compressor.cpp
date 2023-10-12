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

#include <folly/gen/Base.h>

#include "dwarfs/block_compressor.h"
#include "dwarfs/error.h"
#include "dwarfs/fstypes.h"
#include "dwarfs/option_map.h"

namespace dwarfs {

block_compressor::block_compressor(const std::string& spec) {
  impl_ = compression_registry::instance().make_compressor(spec);
}

block_decompressor::block_decompressor(compression_type type,
                                       const uint8_t* data, size_t size,
                                       std::vector<uint8_t>& target) {
  impl_ = compression_registry::instance().make_decompressor(
      type, std::span<uint8_t const>(data, size), target);
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
                                        std::span<uint8_t const> data,
                                        std::vector<uint8_t>& target) const {
  auto fit = factories_.find(type);

  if (fit == factories_.end()) {
    DWARFS_THROW(runtime_error,
                 "unsupported compression type: " + get_compression_name(type));
  }

  return fit->second->make_decompressor(data, target);
}

void compression_registry::for_each_algorithm(
    std::function<void(compression_type, compression_info const&)> const& fn)
    const {
  using namespace folly::gen;

  from(factories_) | get<0>() | order |
      [this, &fn](compression_type type) { fn(type, *factories_.at(type)); };
}

compression_registry::compression_registry() = default;
compression_registry::~compression_registry() = default;

} // namespace dwarfs
