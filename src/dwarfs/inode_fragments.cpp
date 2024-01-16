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

#include <numeric>
#include <ostream>
#include <sstream>

#include "dwarfs/inode_fragments.h"

namespace dwarfs {

void single_inode_fragment::add_chunk(size_t block, size_t offset,
                                      size_t size) {
  if (!chunks_.empty()) {
    auto& last = chunks_.back();
    if (last.block() == block &&
        last.offset().value() + last.size().value() == offset) [[unlikely]] {
      // merge chunks
      last.size() = last.size().value() + size;
      return;
    }
  }

  thrift::metadata::chunk c;
  c.block() = block;
  c.offset() = offset;
  c.size() = size;
  chunks_.push_back(std::move(c));
}

bool single_inode_fragment::chunks_are_consistent() const {
  if (length_ > 0 && chunks_.empty()) {
    return false;
  }

  auto total_chunks_len = std::accumulate(
      chunks_.begin(), chunks_.end(), file_off_t{0},
      [](auto acc, auto const& c) { return acc + c.get_size(); });

  return total_chunks_len == length_;
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

std::unordered_map<fragment_category, file_off_t>
inode_fragments::get_category_sizes() const {
  std::unordered_map<fragment_category, file_off_t> result;

  for (auto const& f : span()) {
    result[f.category()] += f.size();
  }

  return result;
}

} // namespace dwarfs
