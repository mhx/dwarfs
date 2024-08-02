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

#include <vector>

#include <dwarfs/tool/call_sys_main_iolayer.h>

extern "C" int dwarfs_wcwidth(int ucs);

namespace dwarfs::tool {

namespace {

template <typename T>
int call_sys_main_iolayer_impl(std::span<T> args, iolayer const& iol,
                               int (*main)(int, sys_char**, iolayer const&)) {
  std::vector<sys_string> argv;
  std::vector<sys_char*> argv_ptrs;
  argv.reserve(args.size());
  argv_ptrs.reserve(args.size());
  for (auto const& arg : args) {
    argv.emplace_back(string_to_sys_string(std::string(arg)));
    argv_ptrs.emplace_back(argv.back().data());
  }
  return main(argv_ptrs.size(), argv_ptrs.data(), iol);
}

} // namespace

int call_sys_main_iolayer(std::span<std::string_view> args, iolayer const& iol,
                          int (*main)(int, sys_char**, iolayer const&)) {
  return call_sys_main_iolayer_impl(args, iol, main);
}

int call_sys_main_iolayer(std::span<std::string> args, iolayer const& iol,
                          int (*main)(int, sys_char**, iolayer const&)) {
  return call_sys_main_iolayer_impl(args, iol, main);
}

} // namespace dwarfs::tool
