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
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <numeric>
#include <ostream>
#include <sstream>

#include <dwarfs/conv.h>
#include <dwarfs/writer/inode_fragments.h>

namespace dwarfs::writer {

void inode_fragments::append(inode_fragments const& other) {
  fragments_.insert(fragments_.end(), other.fragments_.begin(),
                    other.fragments_.end());
}

file_size_t inode_fragments::total_size() const {
  return std::accumulate(
      fragments_.begin(), fragments_.end(), file_size_t{0},
      [](auto acc, auto const& f) { return acc + f.size(); });
}

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

std::unordered_map<fragment_category, file_size_t>
inode_fragments::get_category_sizes() const {
  std::unordered_map<fragment_category, file_size_t> result;

  for (auto const& f : span()) {
    result[f.category()] += f.size();
  }

  return result;
}

std::size_t inode_fragments::size_in_bytes() const {
  std::size_t total = sizeof(inode_fragments);
  if (fragments_.size() > 1) {
    total += fragments_.size() * sizeof(single_inode_fragment);
  }
  for (auto const& f : span()) {
    total += f.allocated_size_in_bytes();
  }
  return total;
}

} // namespace dwarfs::writer
