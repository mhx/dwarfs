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

#include <algorithm>

#include <boost/range/irange.hpp>

#include <fmt/format.h>

#include <dwarfs/error.h>
#include <dwarfs/file_type.h>

#include <dwarfs/internal/metadata_utils.h>

#include <dwarfs/gen-cpp2/metadata_types.h>

namespace dwarfs::internal {

namespace {

size_t find_inode_rank_offset_impl(inode_rank rank, size_t size,
                                   auto&& get_inode_mode) {
  auto range = boost::irange<size_t>(0, size);

  auto it = std::lower_bound(range.begin(), range.end(), rank,
                             [&](auto inode, inode_rank r) {
                               return get_inode_rank(get_inode_mode(inode)) < r;
                             });

  return *it;
}

} // namespace

inode_rank get_inode_rank(uint32_t mode) {
  switch (posix_file_type::from_mode(mode)) {
  case posix_file_type::directory:
    return inode_rank::INO_DIR;
  case posix_file_type::symlink:
    return inode_rank::INO_LNK;
  case posix_file_type::regular:
    return inode_rank::INO_REG;
  case posix_file_type::block:
  case posix_file_type::character:
    return inode_rank::INO_DEV;
  case posix_file_type::socket:
  case posix_file_type::fifo:
    return inode_rank::INO_OTH;
  default:
    DWARFS_THROW(runtime_error,
                 fmt::format("unknown file type: {:#06x}", mode));
  }
}

size_t find_inode_rank_offset(
    ::apache::thrift::frozen::Layout<thrift::metadata::metadata>::View meta,
    inode_rank rank) {
  auto get_mode = [&](auto index) {
    return meta.modes()[meta.inodes()[index].mode_index()];
  };

  if (meta.dir_entries()) {
    return find_inode_rank_offset_impl(
        rank, meta.inodes().size(),
        [&](auto inode) { return get_mode(inode); });
  }

  return find_inode_rank_offset_impl(
      rank, meta.entry_table_v2_2().size(),
      [&](auto inode) { return get_mode(meta.entry_table_v2_2()[inode]); });
}

size_t find_inode_rank_offset(thrift::metadata::metadata const& meta,
                              inode_rank rank) {
  auto get_mode = [&](auto index) {
    return meta.modes().value().at(
        meta.inodes().value().at(index).mode_index().value());
  };

  if (meta.dir_entries().has_value()) {
    return find_inode_rank_offset_impl(
        rank, meta.inodes()->size(),
        [&](auto inode) { return get_mode(inode); });
  }

  return find_inode_rank_offset_impl(
      rank, meta.entry_table_v2_2()->size(), [&](auto inode) {
        return get_mode(meta.entry_table_v2_2().value().at(inode));
      });
}

} // namespace dwarfs::internal
