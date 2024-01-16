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
#include <unordered_map>

#include <folly/small_vector.h>

#include "dwarfs/fragment_category.h"
#include "dwarfs/types.h"

#include "dwarfs/gen-cpp2/metadata_types.h"

namespace dwarfs {

class single_inode_fragment {
 public:
  single_inode_fragment(fragment_category category, file_off_t length)
      : category_{category}
      , length_{length} {}

  fragment_category category() const { return category_; }
  file_off_t length() const { return length_; }
  file_off_t size() const { return length_; }

  void add_chunk(size_t block, size_t offset, size_t size);

  std::span<thrift::metadata::chunk const> chunks() const { return chunks_; }

  void extend(file_off_t length) { length_ += length; }

  bool chunks_are_consistent() const;

 private:
  fragment_category category_;
  file_off_t length_;
  folly::small_vector<thrift::metadata::chunk, 1> chunks_;
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

  single_inode_fragment const& back() const { return fragments_.back(); }
  single_inode_fragment& back() { return fragments_.back(); }

  auto begin() const { return fragments_.begin(); }
  auto begin() { return fragments_.begin(); }

  auto end() const { return fragments_.end(); }
  auto end() { return fragments_.end(); }

  size_t size() const { return fragments_.size(); }

  bool empty() const { return fragments_.empty(); }

  void clear() { fragments_.clear(); }

  fragment_category get_single_category() const {
    assert(fragments_.size() == 1);
    return fragments_.at(0).category();
  }

  explicit operator bool() const { return !empty(); }

  std::ostream&
  to_stream(std::ostream& os,
            mapper_function_type const& mapper = mapper_function_type()) const;
  std::string
  to_string(mapper_function_type const& mapper = mapper_function_type()) const;

  std::unordered_map<fragment_category, file_off_t> get_category_sizes() const;

 private:
  folly::small_vector<single_inode_fragment, 1> fragments_;
};

inline std::ostream& operator<<(std::ostream& os, inode_fragments const& frag) {
  return frag.to_stream(os);
}

} // namespace dwarfs
