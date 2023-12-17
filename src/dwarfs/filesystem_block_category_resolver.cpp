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

#include <folly/container/Enumerate.h>

#include "dwarfs/filesystem_block_category_resolver.h"

namespace dwarfs {

filesystem_block_category_resolver::filesystem_block_category_resolver(
    std::vector<std::string> categories)
    : categories_{std::move(categories)} {
  for (auto const& [i, name] : folly::enumerate(categories_)) {
    if (!category_map_.emplace(name, i).second) {
      throw std::runtime_error(
          fmt::format("duplicate category name: '{}'", name));
    }
  }
}

std::string_view filesystem_block_category_resolver::category_name(
    fragment_category::value_type c) const {
  return categories_.at(c);
}

std::optional<fragment_category::value_type>
filesystem_block_category_resolver::category_value(
    std::string_view name) const {
  if (auto it = category_map_.find(name); it != category_map_.end()) {
    return it->second;
  }
  return std::nullopt;
}

} // namespace dwarfs
