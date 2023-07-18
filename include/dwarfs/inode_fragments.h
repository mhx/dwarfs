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

#include <functional>
#include <iosfwd>
#include <span>
#include <string>

#include <folly/small_vector.h>

#include "dwarfs/fragment_category.h"
#include "dwarfs/types.h"

namespace dwarfs {

class single_inode_fragment {
 public:
  single_inode_fragment(fragment_category category, file_off_t length)
      : category_{category}
      , length_{length} {}

  fragment_category category() const { return category_; }
  file_off_t length() const { return length_; }
  file_off_t size() const { return length_; }

 private:
  fragment_category category_;
  file_off_t length_;
};

class inode_fragments {
 public:
  using mapper_function_type =
      std::function<std::string(fragment_category::value_type)>;

  inode_fragments() = default;

  single_inode_fragment&
  emplace_back(fragment_category category, file_off_t length) {
    return fragments_.emplace_back(category, length);
  }

  std::span<single_inode_fragment const> span() const { return fragments_; }

  bool empty() const { return fragments_.empty(); }

  void clear() { fragments_.clear(); }

  explicit operator bool() const { return !empty(); }

  std::ostream&
  to_stream(std::ostream& os,
            mapper_function_type const& mapper = mapper_function_type()) const;
  std::string
  to_string(mapper_function_type const& mapper = mapper_function_type()) const;

 private:
  folly::small_vector<single_inode_fragment, 1> fragments_;
};

inline std::ostream& operator<<(std::ostream& os, inode_fragments const& frag) {
  return frag.to_stream(os);
}

} // namespace dwarfs
