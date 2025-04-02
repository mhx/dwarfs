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

#include <fmt/format.h>

#include <dwarfs/error.h>
#include <dwarfs/reader/block_range.h>

#include <dwarfs/reader/internal/cached_block.h>

namespace dwarfs::reader {

block_range::block_range(uint8_t const* data, size_t offset, size_t size)
    : span_{data + offset, size} {
  if (!data) {
    DWARFS_THROW(runtime_error, "block_range: block data is null");
  }
}

block_range::block_range(std::shared_ptr<internal::cached_block const> block,
                         size_t offset, size_t size)
    : span_{block->data() + offset, size}
    , block_{std::move(block)} {
  if (!block_->data()) {
    DWARFS_THROW(runtime_error, "block_range: block data is null");
  }
  if (offset + size > block_->range_end()) {
    DWARFS_THROW(runtime_error,
                 fmt::format("block_range: size out of range ({0} > {1})",
                             offset + size, block_->range_end()));
  }
}

} // namespace dwarfs::reader
