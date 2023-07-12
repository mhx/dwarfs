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

#include "dwarfs/tool.h"
#include "dwarfs/version.h"

namespace dwarfs {

std::string
tool_header(std::string_view tool_name, std::string_view extra_info) {
  return fmt::format(
      // clang-format off
    R"(     ___                  ___ ___)""\n"
    R"(    |   \__ __ ____ _ _ _| __/ __|         Deduplicating Warp-speed)""\n"
    R"(    | |) \ V  V / _` | '_| _|\__ \      Advanced Read-only File System)""\n"
    R"(    |___/ \_/\_/\__,_|_| |_| |___/         by Marcus Holland-Moritz)""\n\n"
      // clang-format on
      "{} ({}{})\nbuilt on {}\n\n",
      tool_name, PRJ_GIT_ID, extra_info, PRJ_BUILD_ID);
}

} // namespace dwarfs
