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

#include <iostream>
#include <sstream>

#include <dwarfs/logger.h>
#include <dwarfs/os_access_generic.h>
#include <dwarfs/reader/filesystem_options.h>
#include <dwarfs/reader/filesystem_v2.h>
#include <dwarfs/reader/fsinfo_options.h>

using namespace dwarfs;

int main(int argc, char** argv) {
  if (argc < 2) {
    return 1;
  }

  // stream_logger lgr(std::cerr);
  null_logger lgr;
  os_access_generic os;

#ifdef __AFL_LOOP
  while (__AFL_LOOP(10000))
#endif
  {
    try {
      std::ostringstream oss;
      reader::filesystem_v2 fs(lgr, os, argv[1],
                               {.metadata = {.check_consistency = true}});
      fs.dump(oss, {.features = reader::fsinfo_features::all()});
    } catch (std::exception const& e [[maybe_unused]]) {
      // std::cerr << "Exception: " << e.what() << "\n";
    }
  }

  return 0;
}
