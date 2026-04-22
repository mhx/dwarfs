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

#include <dwarfs/metadata_defs.h>
#include <dwarfs/types.h>
#include <dwarfs/writer/fragment_category.h>

namespace dwarfs::writer {

class single_inode_fragment {
 public:
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

  fragment_category category() const { return category_; }

  file_size_t size() const { return bits_ & kChunkBitsSizeMask; }

  void extend(file_size_t length) {
    bits_ = (this->size() + length) | (bits_ & kChunkBitsHoleBit);
  }

 private:
  fragment_category category_;
  uint64_t bits_;
};

} // namespace dwarfs::writer
