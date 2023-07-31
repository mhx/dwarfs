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

#include <fmt/format.h>

#include <folly/String.h>

#include "dwarfs/categorizer.h"
#include "dwarfs/category_parser.h"

namespace dwarfs {

category_parser::category_parser(std::shared_ptr<categorizer_manager> catmgr)
    : catmgr_{catmgr} {}

std::vector<fragment_category::value_type>
category_parser::parse(std::string_view arg) const {
  if (!catmgr_) {
    throw std::runtime_error(
        "cannot configure category-specific options without any categorizers");
  }

  std::vector<fragment_category::value_type> rv;
  std::vector<std::string_view> categories;

  folly::split(',', arg, categories);
  rv.reserve(categories.size());

  for (auto const& name : categories) {
    if (auto val = catmgr_->category_value(name)) {
      rv.emplace_back(*val);
    } else {
      throw std::range_error(fmt::format("unknown category: '{}'", name));
    }
  }

  return rv;
}

std::string
category_parser::to_string(fragment_category::value_type const& val) const {
  return std::string(catmgr_->category_name(val));
}

} // namespace dwarfs
