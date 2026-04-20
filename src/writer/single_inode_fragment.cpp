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
#include <utility>

#include <dwarfs/conv.h>
#include <dwarfs/writer/single_inode_fragment.h>

namespace dwarfs::writer {

void single_inode_fragment::add_chunk(size_t block, size_t offset,
                                      size_t size) {
  if (!chunks_.empty()) {
    auto last = chunks_.back();

    if (get<kKindField>(last) == chunk_kind::data &&
        get<kBlockField>(last) == to<block_type>(block) &&
        std::cmp_equal(get<kOffsetField>(last) + get<kSizeField>(last), offset))
        [[unlikely]] {
      // merge chunks
      get<kSizeField>(last) += size;
      return;
    }
  }

  chunks_.push_back(packed_chunk_tuple{
      block,
      offset,
      size,
      chunk_kind::data,
  });
}

void single_inode_fragment::add_hole(file_size_t size) {
  chunks_.push_back(packed_chunk_tuple{
      0,
      0,
      size,
      chunk_kind::hole,
  });
}

bool single_inode_fragment::chunks_are_consistent() const {
  auto const frag_size = size();

  if (frag_size > 0 && chunks_.empty()) {
    return false;
  }

  auto const chs = chunks();
  auto const total_chunks_len =
      std::accumulate(chs.begin(), chs.end(), file_size_t{0},
                      [](auto acc, auto const& c) { return acc + c.size(); });

  return total_chunks_len == frag_size;
}

std::size_t single_inode_fragment::allocated_size_in_bytes() const {
  return chunks_.is_inline() ? 0 : chunks_.size_in_bytes();
}

} // namespace dwarfs::writer
