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

#pragma once

#include <cassert>
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <span>
#include <string>
#include <unordered_map>

#include <dwarfs/small_vector.h>
#include <dwarfs/types.h>
#include <dwarfs/writer/fragment_category.h>
#include <dwarfs/writer/single_inode_fragment.h>

namespace dwarfs::writer {

class inode_fragments {
 public:
  using mapper_function_type =
      std::function<std::string(fragment_category::value_type)>;

  inode_fragments() = default;

  single_inode_fragment&
  emplace_back(fragment_category category, file_size_t length) {
    return fragments_.emplace_back(category, length);
  }

  single_inode_fragment&
  emplace_back(single_inode_fragment::hole_tag, fragment_category category,
               file_size_t length) {
    return fragments_.emplace_back(single_inode_fragment::hole, category,
                                   length);
  }

  single_inode_fragment const& back() const { return fragments_.back(); }
  single_inode_fragment& back() { return fragments_.back(); }

  auto begin() const { return fragments_.begin(); }
  auto begin() { return fragments_.begin(); }

  auto end() const { return fragments_.end(); }
  auto end() { return fragments_.end(); }

  single_inode_fragment const& operator[](size_t index) const {
    return fragments_[index];
  }

  void append(inode_fragments const& other);

  // NOLINTNEXTLINE(bugprone-exception-escape,cppcoreguidelines-noexcept-swap,performance-noexcept-swap)
  void swap(inode_fragments& other) { fragments_.swap(other.fragments_); }

  size_t size() const { return fragments_.size(); }

  bool empty() const { return fragments_.empty(); }

  void clear() { fragments_.clear(); }

  fragment_category get_single_category() const {
    assert(fragments_.size() == 1);
    return fragments_.at(0).category();
  }

  explicit operator bool() const { return !empty(); }

  file_size_t total_size() const;

  std::ostream&
  to_stream(std::ostream& os,
            mapper_function_type const& mapper = mapper_function_type()) const;
  std::string
  to_string(mapper_function_type const& mapper = mapper_function_type()) const;

 private:
  small_vector<single_inode_fragment, 1> fragments_;
};

} // namespace dwarfs::writer
