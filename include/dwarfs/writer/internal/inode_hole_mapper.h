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

#include <cstdint>
#include <unordered_map>
#include <vector>

#include <dwarfs/types.h>

namespace dwarfs::thrift::metadata {

class chunk;

} // namespace dwarfs::thrift::metadata

namespace dwarfs::writer::internal {

class inode_hole_mapper {
 public:
  inode_hole_mapper(size_t hole_block_index, size_t block_size,
                    size_t max_data_chunk_size);

  void map_hole(dwarfs::thrift::metadata::chunk& out, file_size_t size);

 private:
  size_t hole_block_index_{0};
  int block_size_bits_{0};
  uint64_t inline_hole_size_limit_{0};
  std::vector<uint64_t> large_hole_sizes_;
  std::unordered_map<uint64_t, size_t> large_hole_size_map_;
};

} // namespace dwarfs::writer::internal
