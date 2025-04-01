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

#include <functional>
#include <string_view>
#include <unordered_map>

#include <dwarfs/compressor_factory.h>
#include <dwarfs/decompressor_factory.h>

namespace dwarfs {

class library_dependencies;

namespace detail {

template <typename FactoryT, compression_type Type>
struct compression_registrar;

class compression_registry_base {
 protected:
  void register_name(compression_type type, std::string_view name);

  std::unordered_map<std::string, compression_type> names_;
};

template <typename FactoryT, typename InfoT>
class compression_registry : public compression_registry_base {
 public:
  compression_registry();

  void register_factory(compression_type type,
                        std::unique_ptr<FactoryT const>&& factory);

  void for_each_algorithm(
      std::function<void(compression_type, InfoT const&)> const& fn) const;

  void add_library_dependencies(library_dependencies& deps) const;

 protected:
  template <compression_type Type>
  void do_register();

  FactoryT const& get_factory(compression_type type) const;

 private:
  std::unordered_map<compression_type, std::unique_ptr<FactoryT const>>
      factories_;
};

using compressor_registry_base =
    compression_registry<compressor_factory, compressor_info>;
using decompressor_registry_base =
    compression_registry<decompressor_factory, decompressor_info>;

#define DWARFS_DETAIL_COMP_REGISTRAR_(type, name, value)                       \
  template <>                                                                  \
  struct compression_registrar<type##_factory, compression_type::name> {       \
    static std::unique_ptr<type##_factory> reg();                              \
  }

#define DWARFS_COMPRESSION_TYPE_ENUMERATION_(name, value)                      \
  DWARFS_DETAIL_COMP_REGISTRAR_(compressor, name, value);                      \
  DWARFS_DETAIL_COMP_REGISTRAR_(decompressor, name, value);

#define DWARFS_NO_SEPARATOR_
DWARFS_COMPRESSION_TYPE_LIST(DWARFS_COMPRESSION_TYPE_ENUMERATION_,
                             DWARFS_NO_SEPARATOR_)
#undef DWARFS_COMPRESSION_TYPE_ENUMERATION_
#undef DWARFS_NO_SEPARATOR_

} // namespace detail
} // namespace dwarfs

#define REGISTER_COMPRESSOR_FACTORY(factory)                                   \
  std::unique_ptr<dwarfs::compressor_factory>                                  \
  dwarfs::detail::compression_registrar<dwarfs::compressor_factory,            \
                                        factory::type>::reg() {                \
    return std::make_unique<factory>();                                        \
  }

#define REGISTER_DECOMPRESSOR_FACTORY(factory)                                 \
  std::unique_ptr<dwarfs::decompressor_factory>                                \
  dwarfs::detail::compression_registrar<dwarfs::decompressor_factory,          \
                                        factory::type>::reg() {                \
    return std::make_unique<factory>();                                        \
  }
