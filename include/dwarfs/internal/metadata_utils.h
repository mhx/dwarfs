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

#include <cstdint>

#include <thrift/lib/cpp2/frozen/FrozenUtil.h>

#include <dwarfs/gen-cpp2/metadata_layouts.h>
#include <dwarfs/gen-cpp2/metadata_types.h>

namespace dwarfs::internal {

// This represents the order in which inodes are stored in inodes
// (or entry_table_v2_2 for older file systems)
enum class inode_rank {
  INO_DIR,
  INO_LNK,
  INO_REG,
  INO_DEV,
  INO_OTH,
};

inode_rank get_inode_rank(uint32_t mode);

size_t find_inode_rank_offset(
    ::apache::thrift::frozen::Layout<thrift::metadata::metadata>::View meta,
    inode_rank rank);

size_t
find_inode_rank_offset(thrift::metadata::metadata const& meta, inode_rank rank);

} // namespace dwarfs::internal
