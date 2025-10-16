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

#include <functional>
#include <string>
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
  compression_type get_type(std::string const& name) const;

 private:
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
