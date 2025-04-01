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

#pragma once

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <vector>

#include <range/v3/algorithm/sort.hpp>
#include <range/v3/range/conversion.hpp>
#include <range/v3/view/map.hpp>

#include <dwarfs/config.h>
#include <dwarfs/library_dependencies.h>
#include <dwarfs/detail/compression_registry.h>
#include <dwarfs/fstypes.h>

namespace dwarfs::detail {

template <typename FactoryT, typename InfoT>
void compression_registry<FactoryT, InfoT>::register_factory(
    compression_type type, std::unique_ptr<FactoryT const>&& factory) {
  auto name = factory->name();

  register_name(type, name);

  if (!factories_.emplace(type, std::move(factory)).second) {
    std::cerr << "compression factory type conflict (" << name << ", "
              << static_cast<int>(type) << ")\n";
    ::abort();
  }
}

template <typename FactoryT, typename InfoT>
void compression_registry<FactoryT, InfoT>::for_each_algorithm(
    std::function<void(compression_type, InfoT const&)> const& fn) const {
  auto types = factories_ | ranges::views::keys | ranges::to<std::vector>;

  ranges::sort(types);

  for (auto type : types) {
    fn(type, *factories_.at(type));
  }
}

template <typename FactoryT, typename InfoT>
void compression_registry<FactoryT, InfoT>::add_library_dependencies(
    library_dependencies& deps) const {
  this->for_each_algorithm(
      [&](compression_type, InfoT const& info) {
        for (auto const& lib : info.library_dependencies()) {
          deps.add_library(lib);
        }
      });
}

template <typename FactoryT, typename InfoT>
template <compression_type Type>
void compression_registry<FactoryT, InfoT>::do_register() {
  this->register_factory(Type, compression_registrar<FactoryT, Type>::reg());
}

template <typename FactoryT, typename InfoT>
FactoryT const& compression_registry<FactoryT, InfoT>::get_factory(
    compression_type type) const {
  auto it = factories_.find(type);

  if (it == factories_.end()) {
    DWARFS_THROW(runtime_error,
                 "unsupported compression type: " + get_compression_name(type));
  }

  return *it->second;
}

template <typename FactoryT, typename InfoT>
compression_registry<FactoryT, InfoT>::compression_registry() {
  using namespace ::dwarfs::detail;
  using enum compression_type;

  do_register<NONE>();
#ifdef DWARFS_HAVE_LIBBROTLI
  do_register<BROTLI>();
#endif
#ifdef DWARFS_HAVE_FLAC
  do_register<FLAC>();
#endif
#ifdef DWARFS_HAVE_LIBLZ4
  do_register<LZ4>();
  do_register<LZ4HC>();
#endif
#ifdef DWARFS_HAVE_LIBLZMA
  do_register<LZMA>();
#endif
#ifdef DWARFS_HAVE_RICEPP
  do_register<RICEPP>();
#endif
#ifdef DWARFS_HAVE_LIBZSTD
  do_register<ZSTD>();
#endif
}

} // namespace dwarfs::detail
