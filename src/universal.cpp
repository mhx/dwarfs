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
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <string_view>
#include <vector>

#include <folly/String.h>
#include <folly/gen/String.h>
#include <folly/portability/Windows.h>

#ifdef _WIN32
#include <delayimp.h>
#endif

#include "dwarfs/safe_main.h"
#include "dwarfs/tool.h"
#include "dwarfs/util.h"
#include "dwarfs_tool_main.h"

namespace {

using namespace dwarfs;

#ifdef _MSC_VER
FARPROC WINAPI delay_hook(unsigned dliNotify, PDelayLoadInfo pdli) {
  switch (dliNotify) {
  case dliFailLoadLib:
    std::cerr << "failed to load " << pdli->szDll << "\n";
    break;

  case dliFailGetProc:
    std::cerr << "failed to load symbol from " << pdli->szDll << "\n";
    break;

  default:
    return NULL;
  }

  ::exit(1);
}
#endif

#ifdef _WIN32
#define EXE_EXT ".exe"
#else
#define EXE_EXT ""
#endif

std::map<std::string_view, int (*)(int, sys_char**)> const functions{
#ifndef WITHOUT_FUSE
    {"dwarfs", &dwarfs_main},
#endif
    {"mkdwarfs", &mkdwarfs_main},
    {"dwarfsck", &dwarfsck_main},
    {"dwarfsextract", &dwarfsextract_main},
    // {"dwarfsbench", &dwarfsbench_main},
};

} // namespace

#ifdef _MSC_VER
extern "C" const PfnDliHook __pfnDliFailureHook2 = delay_hook;
#endif

int SYS_MAIN(int argc, sys_char** argv) {
  if (argc > 1) {
    auto tool_arg = sys_string_to_string(argv[1]);
    if (tool_arg.starts_with("--tool=")) {
      if (auto it = functions.find(tool_arg.substr(7)); it != functions.end()) {
        std::vector<sys_char*> argv_copy;
        argv_copy.reserve(argc - 1);
        argv_copy.emplace_back(argv[0]);
        std::copy(argv + 2, argv + argc, std::back_inserter(argv_copy));
        return safe_main(
            [&] { return it->second(argc - 1, argv_copy.data()); });
      }
    }
  }

  auto path = std::filesystem::path(argv[0]);

  if (path.extension().string() == EXE_EXT) {
    if (auto it = functions.find(path.stem().string()); it != functions.end()) {
      return safe_main([&] { return it->second(argc, argv); });
    }
  }

  using namespace folly::gen;

  auto tools = from(functions) | get<0>() | unsplit(", ");

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
