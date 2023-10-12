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

#include <array>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "dwarfs/gen-cpp2/metadata_layouts.h"

namespace dwarfs {

class logger;

class string_table {
 public:
  using LegacyTableView =
      ::apache::thrift::frozen::View<std::vector<std::string>>;
  using PackedTableView =
      ::apache::thrift::frozen::View<thrift::metadata::string_table>;

  struct pack_options {
    pack_options(bool pack_data = true, bool pack_index = true,
                 bool force_pack_data = false)
        : pack_data{pack_data}
        , pack_index{pack_index}
        , force_pack_data{force_pack_data} {}

    bool pack_data;
    bool pack_index;
    bool force_pack_data;
  };

  string_table(logger& lgr, std::string_view name, PackedTableView v);
  string_table(LegacyTableView v);

  std::string operator[](size_t index) const { return impl_->lookup(index); }

  std::vector<std::string> unpack() const { return impl_->unpack(); }

  bool is_packed() const { return impl_->is_packed(); }

  size_t unpacked_size() const { return impl_->unpacked_size(); }

  static thrift::metadata::string_table
  pack(std::span<std::string const> input,
       pack_options const& options = pack_options());

  static thrift::metadata::string_table
  pack(std::span<std::string_view const> input,
       pack_options const& options = pack_options());

  static thrift::metadata::string_table
  pack(std::vector<std::string> const& input,
       pack_options const& options = pack_options()) {
    return pack(std::span(input.data(), input.size()), options);
  }

  template <size_t N>
  static thrift::metadata::string_table
  pack(std::array<std::string_view, N> const& input,
       pack_options const& options = pack_options()) {
    return pack(std::span(input.data(), input.size()), options);
  }

  class impl {
   public:
    virtual ~impl() = default;

    virtual std::string lookup(size_t index) const = 0;
    virtual std::vector<std::string> unpack() const = 0;
    virtual bool is_packed() const = 0;
    virtual size_t unpacked_size() const = 0;
  };

 private:
  template <typename T>
  static thrift::metadata::string_table
  pack_generic(std::span<T const> input, pack_options const& options);

  std::unique_ptr<impl const> impl_;
};

} // namespace dwarfs
