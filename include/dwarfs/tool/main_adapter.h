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

#pragma once

#include <span>
#include <string>
#include <string_view>

#include <dwarfs/tool/sys_char.h>

namespace dwarfs::tool {

struct iolayer;

class main_adapter {
 public:
  using main_fn_type = int (*)(int, sys_char**, iolayer const&);

  explicit main_adapter(main_fn_type main_fn)
      : main_fn_(main_fn) {}

  int operator()(int argc, sys_char** argv) const;
  int operator()(std::span<std::string const> args, iolayer const& iol) const;
  int operator()(std::span<std::string_view const> args,
                 iolayer const& iol) const;

  int safe(int argc, sys_char** argv) const;
  int safe(std::span<std::string const> args, iolayer const& iol) const;
  int safe(std::span<std::string_view const> args, iolayer const& iol) const;

 private:
  main_fn_type main_fn_{nullptr};
};

} // namespace dwarfs::tool
