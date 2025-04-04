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
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <ostream>
#include <string>

#include <fmt/format.h>

#include <dwarfs/error.h>
#include <dwarfs/writer/fragment_order_options.h>

namespace dwarfs::writer {

std::ostream& operator<<(std::ostream& os, fragment_order_mode mode) {
  std::string modestr{"unknown"};

  switch (mode) {
  case fragment_order_mode::NONE:
    modestr = "none";
    break;
  case fragment_order_mode::PATH:
    modestr = "path";
    break;
  case fragment_order_mode::REVPATH:
    modestr = "revpath";
    break;
  case fragment_order_mode::SIMILARITY:
    modestr = "similarity";
    break;
  case fragment_order_mode::NILSIMSA:
    modestr = "nilsimsa";
    break;
  case fragment_order_mode::EXPLICIT:
    modestr = "explicit";
    break;
  }

  return os << modestr;
}

} // namespace dwarfs::writer
