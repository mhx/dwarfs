/* vim:set ts=2 sw=2 sts=2 et: */
/**
 * \author     Marcus Holland-Moritz (github@mhxnet.de)
 * \copyright  Copyright (c) Marcus Holland-Moritz
 *
 * This file is part of dwarfs.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the “Software”), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>

#include <boost/range/irange.hpp>

#include <fmt/format.h>

#include <dwarfs/error.h>
#include <dwarfs/file_type.h>

#include <dwarfs/internal/metadata_utils.h>

#include <dwarfs/gen-cpp-lite/metadata_lite_types.h>

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
