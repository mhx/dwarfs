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
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <bit>
#include <string_view>

#include <fmt/format.h>

#include <dwarfs/error.h>

template <>
struct fmt::formatter<std::endian> : formatter<std::string_view> {
  template <typename FormatContext>
  auto format(std::endian e, FormatContext& ctx) const {
    std::string_view sv{"<unknown endian>"};
    switch (e) {
    case std::endian::little: // unused, there are no little-endian FITS files
      sv = "little";
      break;
    case std::endian::big:
      sv = "big";
      break;
    default:
      DWARFS_PANIC("internal error: unhandled endianness value");
    }
    return formatter<std::string_view>::format(sv, ctx);
  }
};

