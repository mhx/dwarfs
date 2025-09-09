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

#include <dwarfs/bits.h>
#include <dwarfs/metadata_defs.h>

#include <dwarfs/writer/internal/inode_hole_mapper.h>

#include <dwarfs/gen-cpp2/metadata_types.h>

namespace dwarfs::writer::internal {

inode_hole_mapper::inode_hole_mapper(size_t hole_block_index, size_t block_size,
                                     size_t max_data_chunk_size)
    : hole_block_index_{hole_block_index}
    , block_size_bits_{used_bits(block_size)}
    , inline_hole_size_limit_{(UINT64_C(1) << (used_bits(max_data_chunk_size) +
                                               block_size_bits_))} {}

void inode_hole_mapper::map_hole(dwarfs::thrift::metadata::chunk& out,
                                 file_size_t const size) {
  auto const size64 = static_cast<uint64_t>(size);
  uint64_t offset = size64 & ((UINT64_C(1) << block_size_bits_) - 1);

  out.block() = hole_block_index_;

  if (size64 < inline_hole_size_limit_ && offset != kChunkOffsetIsLargeHole) {
    out.offset() = offset;
    out.size() = size64 >> block_size_bits_;
  } else {
    out.offset() = kChunkOffsetIsLargeHole;
    auto [it, inserted] =
        large_hole_size_map_.emplace(size64, large_hole_sizes_.size());
    if (inserted) {
      large_hole_sizes_.push_back(size64);
    }
    out.size() = it->second;
  }
}

} // namespace dwarfs::writer::internal
