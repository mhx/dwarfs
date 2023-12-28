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

#include <iostream>

#include "dwarfs/file_access.h"
#include "dwarfs/file_access_generic.h"
#include "dwarfs/iolayer.h"
#include "dwarfs/os_access_generic.h"
#include "dwarfs/terminal.h"

namespace dwarfs {

iolayer const& iolayer::system_default() {
  static iolayer const iol{
      .os = std::make_shared<os_access_generic>(),
      .term = terminal::create(),
      .file = create_file_access_generic(),
      .in = std::cin,
      .out = std::cout,
      .err = std::cerr,
  };
  return iol;
}

} // namespace dwarfs
