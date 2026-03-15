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

#pragma once

#include <cstdint>

#include <thrift/lib/cpp2/frozen/FrozenUtil.h>

#include <dwarfs/gen-cpp-lite/metadata_lite_layouts.h>
#include <dwarfs/gen-cpp-lite/metadata_lite_types.h>

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
