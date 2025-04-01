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

#include <dwarfs/compressor_registry.h>
#include <dwarfs/error.h>
#include <dwarfs/option_map.h>

#include "compression_registry.h"

namespace dwarfs {

namespace detail {

template class compression_registry<compressor_factory, compressor_info>;

} // namespace detail

compressor_registry::compressor_registry() = default;
compressor_registry::~compressor_registry() = default;

compressor_registry& compressor_registry::instance() {
  static compressor_registry the_instance;
  return the_instance;
}

std::unique_ptr<block_compressor::impl>
compressor_registry::create(std::string_view spec) const {
  option_map om(spec);
  auto nit = names_.find(om.choice());

  if (nit == names_.end()) {
    DWARFS_THROW(runtime_error, "unknown compression: " + om.choice());
  }

  auto obj = get_factory(nit->second).create(om);

  om.report();

  return obj;
}

} // namespace dwarfs
