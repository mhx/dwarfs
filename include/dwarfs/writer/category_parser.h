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
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#include <memory>
#include <vector>

#include <dwarfs/writer/fragment_category.h>

namespace dwarfs::writer {

class category_resolver;

class category_parser {
 public:
  category_parser(std::shared_ptr<category_resolver const> resolver);

  std::vector<fragment_category::value_type> parse(std::string_view arg) const;
  std::string to_string(fragment_category::value_type const& val) const;

 private:
  std::shared_ptr<category_resolver const> resolver_;
};

} // namespace dwarfs::writer
