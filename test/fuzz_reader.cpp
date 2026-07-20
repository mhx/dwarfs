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
#include <sstream>

#include <dwarfs/logger.h>
#include <dwarfs/os_access_generic.h>
#include <dwarfs/reader/detail/file_reader.h>
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
      reader::filesystem_v2 fs(
          lgr, os, argv[1],
          {
              .block_cache = {.max_bytes = 256 * 1024,
                              .sequential_access_detector_threshold = 4},
              .metadata = {.check_consistency = true},
              .inode_reader = {.readahead = 4},
          });
      fs.dump(oss, {.features = reader::fsinfo_features::all()});
      fs.walk([&](auto const& de) {
        auto iv = de.inode();
        if (iv.is_regular_file()) {
          reader::detail::file_reader fr(fs, iv);
          std::vector<char> buffer;
          for (auto const& ei : fr.extents()) {
            if (ei.kind == extent_kind::data) {
              auto const& range = ei.range;
              buffer.resize(range.size());
              auto const num_read = fs.read(iv.inode_num(), buffer.data(),
                                            range.size(), range.offset());
              if (std::cmp_not_equal(num_read, range.size())) {
                throw std::runtime_error("read failed");
              }
            }
          }
        }
      });
    } catch (std::exception const& e [[maybe_unused]]) {
      // std::cerr << "Exception: " << e.what() << "\n";
    }
  }

  return 0;
}
