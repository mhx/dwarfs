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

#include <cstdint>
#include <span>
#include <string_view>

#include <fmt/color.h>
#include <fmt/format.h>

namespace dwarfs::manpage {

struct element {
  fmt::text_style style;
  std::string_view text;
};

struct line {
  uint32_t indent_first;
  uint32_t indent_next;
  std::span<element const> elements;
};

using document = std::span<line const>;

document get_mkdwarfs_manpage();
document get_dwarfs_manpage();
document get_dwarfsck_manpage();
document get_dwarfsextract_manpage();

} // namespace dwarfs::manpage
