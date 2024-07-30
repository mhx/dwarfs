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

#include <dwarfs/safe_main.h>
#include <dwarfs_tool_main.h>

#include <fmt/chrono.h>
#include <iostream>
#include <string_view>
#include <chrono>

void print_with_timestamp(std::ostream& os, std::string_view msg) {
  auto now = std::chrono::system_clock::now();

  os << fmt::format("{:%H:%M:%S}.{:06d} ", now,
                     std::chrono::duration_cast<std::chrono::microseconds>(
                         now.time_since_epoch())
                         .count() %
                         1000000)
     << msg << "\n";
}

int SYS_MAIN(int argc, dwarfs::sys_char** argv) {
  print_with_timestamp(std::cerr, "dwarfs starting");
  return dwarfs::safe_main([&] { return dwarfs::dwarfs_main(argc, argv); });
}
