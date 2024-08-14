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

#include <compare>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <dwarfs/reader/fsinfo_features.h>

namespace dwarfs::reader {

enum class block_access_level {
  no_access,
  no_verify,
  unrestricted,
};

inline auto operator<=>(block_access_level lhs, block_access_level rhs) {
  return static_cast<std::underlying_type_t<block_access_level>>(lhs) <=>
         static_cast<std::underlying_type_t<block_access_level>>(rhs);
}

struct fsinfo_options {
  fsinfo_features features;
  block_access_level block_access{block_access_level::unrestricted};
};

} // namespace dwarfs::reader
