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

#include <sstream>
#include <boost/program_options.hpp>

#include "dwarfs/types.h"

namespace dwarfs {

#ifdef _WIN32
template <typename T>
auto po_sys_value(T* v) {
  return boost::program_options::wvalue<T>(v);
}
#else
template <typename T>
auto po_sys_value(T* v) {
  return boost::program_options::value<T>(v);
}
#endif

// https://stackoverflow.com/questions/45211248/boosts-is-any-of-causes-compile-warning
inline std::vector<std::string> split(const std::string_view &str, char delim) {
  std::vector<std::string> out;
  std::stringstream ss((std::string(str)));
  std::string item;
  while (std::getline(ss, item, delim)) {
      out.push_back(item);
  }
  return out;
}

} // namespace dwarfs
