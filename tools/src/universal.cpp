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

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <string_view>
#include <vector>

#include <folly/portability/Windows.h>

#include <range/v3/range/conversion.hpp>
#include <range/v3/view/join.hpp>
#include <range/v3/view/map.hpp>

#include <dwarfs/tool/main_adapter.h>
#include <dwarfs/tool/tool.h>
#include <dwarfs_tool_main.h>

namespace {

using namespace dwarfs::tool;

#ifdef _WIN32
#define EXE_EXT ".exe"
#else
#define EXE_EXT ""
#endif

std::map<std::string_view, main_adapter::main_fn_type> const functions{
    {"dwarfs", dwarfs_main},
    {"mkdwarfs", mkdwarfs_main},
    {"dwarfsck", dwarfsck_main},
    {"dwarfsextract", dwarfsextract_main},
};

} // namespace

int SYS_MAIN(int argc, sys_char** argv) {
  auto path = std::filesystem::path(argv[0]);

  // first, see if we are called as a copy/hardlink/symlink

  if (auto ext = path.extension().string(); ext.empty() || ext == EXE_EXT) {
    auto stem = path.stem().string();
    if (auto it = functions.find(stem); it != functions.end()) {
      return main_adapter(it->second).safe(argc, argv);
    }

    // see if the stem has an appended version and try removing that
    if (auto pos = stem.find_first_of('-'); pos != std::string::npos &&
                                            pos + 1 < stem.size() &&
                                            std::isdigit(stem[pos + 1])) {
      if (auto it = functions.find(stem.substr(0, pos));
          it != functions.end()) {
        std::cerr << "running " << stem << " as " << stem.substr(0, pos)
                  << "\n";
        return main_adapter(it->second).safe(argc, argv);
      }
    }
  }

  // if not, see if we can find a --tool=... argument

  if (argc > 1) {
    auto tool_arg = sys_string_to_string(argv[1]);
    if (tool_arg.starts_with("--tool=")) {
      if (auto it = functions.find(tool_arg.substr(7)); it != functions.end()) {
        std::vector<sys_char*> argv_copy;
        argv_copy.reserve(argc - 1);
        argv_copy.emplace_back(argv[0]);
        std::copy(argv + 2, argv + argc, std::back_inserter(argv_copy));
        return main_adapter(it->second).safe(argc - 1, argv_copy.data());
      }
    }
  }

  // nope, just print the help

  // The string_view is needed because ranges::views::join() will include
  // the null terminator when using a string literal.
  static std::string_view constexpr kJoiner{", "};
  auto tools = ranges::views::keys(functions) | ranges::views::join(kJoiner) |
               ranges::to<std::string>;

  // clang-format off
  std::cout << tool_header("dwarfs-universal")
            << "Command line options:\n"
            << "  --tool=<name>                     "
                 "which tool to run; available tools are:\n"
            << "                                    "
            << tools << "\n\n";
  // clang-format on

  return 0;
}
