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

#ifdef _WIN32
#include <delayimp.h>
#endif

#include "dwarfs/error.h"
#include "dwarfs/tool.h"
#include "dwarfs_tool_main.h"

namespace {

using namespace dwarfs;

std::string to_narrow_string(sys_char const* str) {
#ifdef _WIN32
  std::wstring_view view(str);
  std::string rv(view.size(), 0);
  std::transform(view.begin(), view.end(), rv.begin(),
                 [](sys_char c) { return static_cast<char>(c); });
  return rv;
#else
  return std::string(str);
#endif
}

#ifdef _WIN32
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

int dwarfs_main_helper(int argc, sys_char** argv) {
  std::vector<std::string> argv_strings;
  std::vector<char*> argv_copy;
  argv_strings.reserve(argc);
  argv_copy.reserve(argc);

  for (int i = 0; i < argc; ++i) {
    argv_strings.push_back(to_narrow_string(argv[i]));
    argv_copy.push_back(argv_strings.back().data());
  }

  return dwarfs_main(argc, argv_copy.data());
}
#endif

#ifdef _WIN32
#define EXE_EXT ".exe"
#else
#define EXE_EXT ""
#endif

std::map<std::string_view, int (*)(int, sys_char**)> const functions{
#ifdef _WIN32
    {"dwarfs", &dwarfs_main_helper},
#else
    {"dwarfs", &dwarfs_main},
#endif
    {"mkdwarfs", &mkdwarfs_main},
    {"dwarfsck", &dwarfsck_main},
    {"dwarfsextract", &dwarfsextract_main},
    // {"dwarfsbench", &dwarfsbench_main},
};

} // namespace

#ifdef _WIN32
extern "C" const PfnDliHook __pfnDliFailureHook2 = delay_hook;
#endif

int SYS_MAIN(int argc, sys_char** argv) {
  if (argc > 1) {
    auto tool_arg = to_narrow_string(argv[1]);
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
            << tools << "\n";
  // clang-format on

  return 0;
}
