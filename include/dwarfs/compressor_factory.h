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
#include <memory>
#include <set>
#include <span>
#include <string>
#include <string_view>

#include <dwarfs/block_compressor.h>

namespace dwarfs {

class option_map;
class library_dependencies;

class compressor_info {
 public:
  virtual ~compressor_info() = default;

  virtual std::string_view name() const = 0;
  virtual std::string_view description() const = 0;
  virtual std::span<std::string const> options() const = 0;
  virtual std::set<std::string> library_dependencies() const = 0;
  virtual bool is_experimental() const = 0;
};

class compressor_factory : public compressor_info {
 public:
  virtual std::unique_ptr<block_compressor::impl>
  create(option_map& om) const = 0;
};

namespace detail {

template <compression_type Type>
compressor_factory const& compressor_factory_for();

#define DWARFS_DECLARE_COMPRESSOR_FACTORY_(name, value)                        \
  template <>                                                                  \
  compressor_factory const& compressor_factory_for<compression_type::name>();
#define DWARFS_NO_SEPARATOR_
DWARFS_COMPRESSION_TYPE_LIST(DWARFS_DECLARE_COMPRESSOR_FACTORY_,
                             DWARFS_NO_SEPARATOR_)
#undef DWARFS_NO_SEPARATOR_
#undef DWARFS_DECLARE_COMPRESSOR_FACTORY_

} // namespace detail

compressor_factory const& get_compressor_factory(compression_type type);
void for_each_compressor(
    std::function<void(compression_type, compressor_info const&)> const& fn);
void add_compressor_library_dependencies(library_dependencies& deps);

} // namespace dwarfs

#define REGISTER_COMPRESSOR_FACTORY(factory)                                   \
  namespace detail {                                                           \
  template <>                                                                  \
  compressor_factory const& compressor_factory_for<factory::type>() {          \
    static factory const instance;                                             \
    return instance;                                                           \
  }                                                                            \
  }
