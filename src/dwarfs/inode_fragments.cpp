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

#include <ostream>
#include <sstream>

#include "dwarfs/inode_fragments.h"

namespace dwarfs {

std::ostream&
inode_fragments::to_stream(std::ostream& os,
                           mapper_function_type const& mapper) const {
  if (empty()) {
    os << "(empty)";
  } else {
    os << "[";
    bool first = true;

    for (auto const& f : span()) {
      if (first) {
        first = false;
      } else {
        os << ", ";
      }

      os << "(";

      auto const& cat = f.category();
      if (mapper) {
        os << mapper(cat.value());
      } else {
        os << cat.value();
      }

      if (cat.has_subcategory()) {
        os << "/" << cat.subcategory();
      }

      os << ", " << f.size() << ")";
    }

    os << "]";
  }

  return os;
}

std::string
inode_fragments::to_string(mapper_function_type const& mapper) const {
  std::ostringstream oss;
  to_stream(oss, mapper);
  return oss.str();
}

} // namespace dwarfs
