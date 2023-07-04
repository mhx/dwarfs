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
#include <map>
#include <string_view>
#include <vector>

#include <folly/String.h>
#include <folly/gen/String.h>

#include "dwarfs/error.h"
#include "dwarfs/tool.h"
#include "dwarfs_tool_main.h"

namespace {

using namespace dwarfs;

std::map<std::string_view, int (*)(int, char**)> const functions{
    {"dwarfs", &dwarfs_main},
    {"mkdwarfs", &mkdwarfs_main},
    {"dwarfsck", &dwarfsck_main},
    {"dwarfsextract", &dwarfsextract_main},
    // {"dwarfsbench", &dwarfsbench_main},
};

} // namespace

int main(int argc, char** argv) {
  if (argc > 1) {
    std::string_view tool_arg(argv[1]);
    if (tool_arg.starts_with("--tool=")) {
      if (auto it = functions.find(tool_arg.substr(7)); it != functions.end()) {
        std::vector<char*> argv_copy;
        argv_copy.reserve(argc - 1);
        argv_copy.emplace_back(argv[0]);
        std::copy(argv + 2, argv + argc, std::back_inserter(argv_copy));
        return dwarfs::safe_main(
            [&] { return it->second(argc - 1, argv_copy.data()); });
      }
    }
  }

#ifndef _WIN32
  auto path = std::filesystem::path(argv[0]);

  if (auto it = functions.find(path.filename().string());
      it != functions.end()) {
    return dwarfs::safe_main([&] { return it->second(argc, argv); });
  }
#endif

  using namespace folly::gen;

  auto tools = from(functions) | get<0>() | unsplit(", ");

  // clang-format off
  std::cout << tool_header("dwarfs-universal")
            << "Command line options:\n"
            << "  --tool=<name>                     "
                 "which tool to run; available tools are:\n"
            << "                                    "
            << tools << "\n";
  // clang-format on

  return 0;
}
