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

#include <dwarfs/checksum.h>
#include <dwarfs/scope_exit.h>

#include <dwarfs/internal/io_ops.h>

int main(int argc, char* argv[]) {
  if (argc == 1) {
    std::cerr << "Usage: " << argv[0] << " <file> ...\n";
    return 1;
  }

  auto const& ops = dwarfs::internal::get_native_memory_mapping_ops();

  for (int i = 1; i < argc; ++i) {
    auto const filename = argv[i];
    std::error_code ec;

    std::cout << filename << "\n";

    auto h = ops.open(filename, ec);
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

    std::cout << "{\n";

    for (auto const& e : extents) {
      std::cout << "  {" << e;

      if (e.kind == dwarfs::extent_kind::data) {
        std::vector<std::byte> buffer(e.range.size());
        auto const read_bytes =
            ops.pread(h, buffer.data(), buffer.size(), e.range.offset(), ec);
        if (ec) {
          std::cerr << "Error reading data: " << ec.message() << "\n";
          return 1;
        }
        if (read_bytes != buffer.size()) {
          std::cerr << "Error reading data: read " << read_bytes
                    << " bytes, expected " << buffer.size() << "\n";
          return 1;
        }

        std::cout << ", \"" + dwarfs::checksum(dwarfs::checksum::xxh3_64)
                                  .update(buffer)
                                  .hexdigest()
                  << "\"";
      }

      std::cout << "},\n";
    }

    std::cout << "}\n";
  }

  return 0;
}
