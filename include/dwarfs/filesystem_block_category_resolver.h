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

#include <string>
#include <unordered_map>
#include <vector>

#include "dwarfs/category_resolver.h"

namespace dwarfs {

class filesystem_block_category_resolver : public category_resolver {
 public:
  filesystem_block_category_resolver() = default;
  explicit filesystem_block_category_resolver(
      std::vector<std::string> categories);

  std::string_view
  category_name(fragment_category::value_type c) const override;
  std::optional<fragment_category::value_type>
  category_value(std::string_view name) const override;

 private:
  std::vector<std::string> categories_;
  std::unordered_map<std::string_view, fragment_category::value_type>
      category_map_;
};

} // namespace dwarfs
