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

#include <filesystem>
#include <iostream>

#include <dwarfs/logger.h>
#include <dwarfs/os_access_generic.h>
#include <dwarfs/reader/filesystem_v2.h>
#include <dwarfs/reader/fsinfo_options.h>
#include <dwarfs/utility/filesystem_extractor.h>

int main(int argc, char** argv) {
  using namespace dwarfs;

  if (argc < 3) {
    std::cerr << "Usage: " << argv[0] << " info|extract <image>\n";
    return 1;
  }

  try {
    os_access_generic os;
    stream_logger lgr(std::cerr);

    reader::filesystem_v2 fs(lgr, os, argv[2]);

    if (std::string_view(argv[1]) == "info") {
      fs.dump(std::cout, {.features = reader::fsinfo_features::for_level(2)});
    } else if (std::string_view(argv[1]) == "extract") {
      utility::filesystem_extractor ex(lgr, os);
      ex.open_disk(std::filesystem::current_path());
      ex.extract(fs);
    } else {
      std::cerr << "Unknown command: " << argv[1] << "\n";
      return 1;
    }
  } catch (std::exception const& e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }

  return 0;
}
