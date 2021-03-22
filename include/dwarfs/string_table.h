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

#include <memory>
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

  static thrift::metadata::string_table
  pack(std::vector<std::string> const& input,
       pack_options const& options = pack_options());

  class impl {
   public:
    virtual ~impl() = default;

    virtual std::string lookup(size_t index) const = 0;
  };

 private:
  std::unique_ptr<impl const> impl_;
};

} // namespace dwarfs
