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
#include <string_view>
#include <unordered_map>

#include "dwarfs/error.h"
#include "dwarfs_tool_main.h"

namespace {

using namespace dwarfs;

std::unordered_map<std::string_view, int (*)(int, char**)> const functions{
    {"mkdwarfs", &mkdwarfs_main},
    {"dwarfsck", &dwarfsck_main},
    {"dwarfsextract", &dwarfsextract_main},
    {"dwarfsbench", &dwarfsbench_main},
};

} // namespace

int main(int argc, char** argv) {
  auto fun = &dwarfs::dwarfs_main;
  auto path = std::filesystem::path(argv[0]);

  if (auto it = functions.find(path.filename().string());
      it != functions.end()) {
    fun = it->second;
  }

  return dwarfs::safe_main([&] { return fun(argc, argv); });
}
