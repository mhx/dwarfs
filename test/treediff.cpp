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

#include "compare_directories.h"

namespace fs = std::filesystem;

int main(int argc, char* argv[]) {
  if (argc != 3) {
    std::cerr << "Usage: " << argv[0] << " <dir1> <dir2>\n";
    return 1;
  }

  fs::path const dir1 = argv[1];
  fs::path const dir2 = argv[2];

  if (!fs::is_directory(dir1)) {
    std::cerr << "Error: " << dir1 << " is not a directory.\n";
    return 1;
  }
  if (!fs::is_directory(dir2)) {
    std::cerr << "Error: " << dir2 << " is not a directory.\n";
    return 1;
  }

  auto const result = dwarfs::test::compare_directories(dir1, dir2);

  if (result.identical()) {
    std::cout << "The directories are identical.\n";
    return 0;
  }

  std::cerr << result;

  return 2;
}
