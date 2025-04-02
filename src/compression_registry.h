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
