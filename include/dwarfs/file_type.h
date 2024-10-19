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

#include <concepts>
#include <cstdint>

namespace dwarfs {

struct posix_file_type {
  static constexpr uint16_t mask = 0170000;

  enum value : uint16_t {
    socket = 0140000,
    symlink = 0120000,
    regular = 0100000,
    block = 0060000,
    directory = 0040000,
    character = 0020000,
    fifo = 0010000,
  };

  static constexpr value from_mode(std::unsigned_integral auto mode)
    requires(sizeof(mode) >= sizeof(uint16_t))
  {
    return static_cast<value>(mode & mask);
  }
};

} // namespace dwarfs
