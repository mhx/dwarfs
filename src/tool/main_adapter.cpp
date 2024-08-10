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

#include <dwarfs/tool/iolayer.h>
#include <dwarfs/tool/main_adapter.h>
#include <dwarfs/tool/safe_main.h>

namespace dwarfs::tool {

namespace {

template <typename T>
int call_sys_main_iolayer(std::span<T> args, iolayer const& iol,
                          main_adapter::main_fn_type main_fn) {
  std::vector<sys_string> argv;
  std::vector<sys_char*> argv_ptrs;
  argv.reserve(args.size());
  argv_ptrs.reserve(args.size());
  for (auto const& arg : args) {
    argv.emplace_back(string_to_sys_string(std::string(arg)));
    argv_ptrs.emplace_back(argv.back().data());
  }
  return main_fn(argv_ptrs.size(), argv_ptrs.data(), iol);
}

} // namespace

int main_adapter::operator()(int argc, sys_char** argv) const {
  return main_fn_(argc, argv, iolayer::system_default());
}

int main_adapter::operator()(std::span<std::string const> args,
                             iolayer const& iol) const {
  return call_sys_main_iolayer(args, iol, main_fn_);
}

int main_adapter::operator()(std::span<std::string_view const> args,
                             iolayer const& iol) const {
  return call_sys_main_iolayer(args, iol, main_fn_);
}

int main_adapter::safe(int argc, sys_char** argv) const {
  return safe_main([&] { return (*this)(argc, argv); });
}

int main_adapter::safe(std::span<std::string const> args,
                       iolayer const& iol) const {
  return safe_main([&] { return (*this)(args, iol); });
}

int main_adapter::safe(std::span<std::string_view const> args,
                       iolayer const& iol) const {
  return safe_main([&] { return (*this)(args, iol); });
}

} // namespace dwarfs::tool
