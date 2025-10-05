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

#include <dwarfs/metadata_defs.h>
#include <dwarfs/small_vector.h>
#include <dwarfs/types.h>
#include <dwarfs/writer/fragment_category.h>

namespace dwarfs::writer {

class single_inode_fragment {
 public:
  class chunk {
   public:
    using block_type = uint32_t;
    using offset_type = uint32_t;
    using size_type = file_size_t;

    struct hole_tag {};
    static constexpr hole_tag hole{};

    chunk() = default;

    chunk(block_type block, offset_type offset, file_size_t size)
        : block_{block}
        , offset_{offset}
        , bits_{static_cast<uint64_t>(size)} {}

    chunk(hole_tag, file_size_t size)
        : bits_{static_cast<uint64_t>(size) | kChunkBitsHoleBit} {}

    bool is_hole() const { return (bits_ & kChunkBitsHoleBit) != 0; }

    bool is_data() const { return !is_hole(); }

    block_type block() const {
      assert(is_data());
      return block_;
    }

    offset_type offset() const {
      assert(is_data());
      return offset_;
    }

    void grow_by(size_type size) {
      bits_ = (this->size() + size) | (bits_ & kChunkBitsHoleBit);
    }

    size_type size() const { return bits_ & kChunkBitsSizeMask; }

   private:
    block_type block_{0};
    offset_type offset_{0};
    uint64_t bits_{0};
  };

  struct hole_tag {};
  static constexpr hole_tag hole{};

  single_inode_fragment(fragment_category category, file_size_t length)
      : category_{category}
      , bits_{static_cast<uint64_t>(length)} {}

  single_inode_fragment(hole_tag, fragment_category category,
                        file_size_t length)
      : category_{category}
      , bits_{static_cast<uint64_t>(length) | kChunkBitsHoleBit} {}

  bool is_hole() const { return (bits_ & kChunkBitsHoleBit) != 0; }

  bool is_data() const { return !is_hole(); }

  fragment_category category() const { return category_; }

  file_size_t size() const { return bits_ & kChunkBitsSizeMask; }

  void add_chunk(size_t block, size_t offset, size_t size);

  void add_hole(file_size_t size);

  std::span<chunk const> chunks() const {
    // TODO: workaround for older boost small_vector
    return {chunks_.data(), chunks_.size()};
  }

  void extend(file_size_t length) {
    bits_ = (this->size() + length) | (bits_ & kChunkBitsHoleBit);
  }

  bool chunks_are_consistent() const;

 private:
  fragment_category category_;
  uint64_t bits_;
  small_vector<chunk, 1> chunks_;
};

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

  std::span<single_inode_fragment const> span() const {
    // TODO: workaround for older boost small_vector
    return {fragments_.data(), fragments_.size()};
  }

  single_inode_fragment const& back() const { return fragments_.back(); }
  single_inode_fragment& back() { return fragments_.back(); }

  auto begin() const { return fragments_.begin(); }
  auto begin() { return fragments_.begin(); }

  auto end() const { return fragments_.end(); }
  auto end() { return fragments_.end(); }

  void append(inode_fragments const& other);

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

  std::unordered_map<fragment_category, file_size_t> get_category_sizes() const;

 private:
  small_vector<single_inode_fragment, 1> fragments_;
};

inline std::ostream& operator<<(std::ostream& os, inode_fragments const& frag) {
  return frag.to_stream(os);
}

} // namespace dwarfs::writer
