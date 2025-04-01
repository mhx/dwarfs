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

#include <cstdlib>
#include <iostream>
#include <string>

#include <dwarfs/detail/compression_registry.h>

namespace dwarfs::detail {

void compression_registry_base::register_name(compression_type type,
                                              std::string_view name) {
  if (!names_.emplace(std::string{name}, type).second) {
    std::cerr << "compression factory name conflict (" << name << ", "
              << static_cast<int>(type) << ")\n";
    ::abort();
  }
}

} // namespace dwarfs::detail
