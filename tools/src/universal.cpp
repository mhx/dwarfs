/* vim:set ts=2 sw=2 sts=2 et: */
/**
 * \author     Marcus Holland-Moritz (github@mhxnet.de)
 * \copyright  Copyright (c) Marcus Holland-Moritz
 *
 * This file is part of dwarfs.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the “Software”), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
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

#include <dwarfs/sorted_array_map.h>
#include <dwarfs/tool/main_adapter.h>
#include <dwarfs/tool/tool.h>
#include <dwarfs_tool_main.h>

namespace {

using namespace dwarfs::tool;
using namespace std::string_view_literals;

constexpr dwarfs::sorted_array_map functions{
#ifdef DWARFS_UNIVERSAL_FUSE_DRIVER
    std::pair{"dwarfs"sv, &dwarfs_main},
#endif
#ifdef DWARFS_UNIVERSAL_MKDWARFS
    std::pair{"mkdwarfs"sv, &mkdwarfs_main},
#endif
#ifdef DWARFS_UNIVERSAL_DWARFSCK
    std::pair{"dwarfsck"sv, &dwarfsck_main},
#endif
#ifdef DWARFS_UNIVERSAL_DWARFSEXTRACT
    std::pair{"dwarfsextract"sv, &dwarfsextract_main},
#endif
};

bool looks_like_executable(std::filesystem::path const& path) {
  auto const ext = path.extension().string();
  return ext.empty()
#ifdef _WIN32
         || ext == ".exe"
#endif
      ;
}

} // namespace

int SYS_MAIN(int argc, sys_char** argv) {
  auto path = std::filesystem::path(argv[0]);

  // first, see if we are called as a copy/hardlink/symlink

  if (looks_like_executable(path)) {
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

  auto tools = ranges::views::keys(functions) | ranges::views::join(", "sv) |
               ranges::to<std::string>;

  // clang-format off
  std::cout << tool_header_nodeps(DWARFS_UNIVERSAL_NAME)
            << "Command line options:\n"
            << "  --tool=<name>                     "
                 "which tool to run; available tools are:\n"
            << "                                    "
            << tools << "\n\n";
  // clang-format on

  return 0;
}
