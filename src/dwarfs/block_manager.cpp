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

#include <cassert>

#include "dwarfs/block_manager.h"

namespace dwarfs {

size_t block_manager::get_logical_block() const {
  size_t block_no;
  {
    std::lock_guard lock{mx_};
    block_no = num_blocks_++;
  }
  return block_no;
}

void block_manager::set_written_block(size_t logical_block,
                                      size_t written_block,
                                      fragment_category::value_type category) {
  std::lock_guard lock{mx_};
  assert(logical_block < num_blocks_);
  if (block_map_.size() < num_blocks_) {
    block_map_.resize(num_blocks_);
  }
  block_map_[logical_block] = std::make_pair(written_block, category);
}

void block_manager::map_logical_blocks(std::vector<chunk_type>& vec) {
  std::lock_guard lock{mx_};
  for (auto& c : vec) {
    size_t block = c.get_block();
    assert(block < num_blocks_);
    c.block() = block_map_[block].value().first;
  }
}

std::vector<fragment_category::value_type>
block_manager::get_written_block_categories() const {
  std::vector<fragment_category::value_type> result;

  {
    std::lock_guard lock{mx_};

    result.resize(num_blocks_);

    for (auto& b : block_map_) {
      auto& mapping = b.value();
      result[mapping.first] = mapping.second;
    }
  }

  return result;
}

} // namespace dwarfs
