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

#include <dwarfs/decompressor_registry.h>
#include <dwarfs/error.h>
#include <dwarfs/fstypes.h>

#include "compression_registry.h"

namespace dwarfs {

namespace detail {

template class compression_registry<decompressor_factory, decompressor_info>;

} // namespace detail

decompressor_registry::decompressor_registry() = default;
decompressor_registry::~decompressor_registry() = default;

decompressor_registry& decompressor_registry::instance() {
  static decompressor_registry the_instance;
  return the_instance;
}

std::unique_ptr<block_decompressor::impl>
decompressor_registry::create(compression_type type,
                              std::span<uint8_t const> data) const {
  return get_factory(type).create(data);
}

} // namespace dwarfs
