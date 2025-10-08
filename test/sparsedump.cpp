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

#include <iostream>

#include <dwarfs/scope_exit.h>

#include <dwarfs/internal/memory_mapping_ops.h>

int main(int argc, char* argv[]) {
  if (argc != 2) {
    std::cerr << "Usage: " << argv[0] << " <file>\n";
    return 1;
  }

  auto const& ops = dwarfs::internal::get_native_memory_mapping_ops();
  std::error_code ec;

  auto h = ops.open(argv[1], ec);
  if (ec) {
    std::cerr << "Error opening file: " << ec.message() << "\n";
    return 1;
  }

  dwarfs::scope_exit close_handle{[&]() { ops.close(h, ec); }};

  auto extents = ops.get_extents(h, ec);
  if (ec) {
    std::cerr << "Error getting extents: " << ec.message() << "\n";
    return 1;
  }

  for (auto const& e : extents) {
    std::cout << e << "\n";
  }

  return 0;
}
