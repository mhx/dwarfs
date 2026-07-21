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

namespace {

std::span<uint8_t const>
checked_subspan(std::span<uint8_t const> data, size_t offset, size_t size) {
  if (offset > data.size()) [[unlikely]] {
    DWARFS_THROW(runtime_error,
                 fmt::format("block_range: offset out of range ({0} > {1})",
                             offset, data.size()));
  }

  if (auto const available = data.size() - offset; size > available)
      [[unlikely]] {
    DWARFS_THROW(runtime_error,
                 fmt::format("block_range: size out of range ({0} + {1} > {2})",
                             offset, size, data.size()));
  }

  if (size > 0 && !data.data()) [[unlikely]] {
    DWARFS_THROW(runtime_error, "block_range: non-empty block data is null");
  }

  return data.subspan(offset, size);
}

} // namespace

block_range::block_range(std::span<uint8_t const> data, size_t offset,
                         size_t size)
    : span_{checked_subspan(data, offset, size)} {}

block_range::block_range(std::shared_ptr<internal::cached_block const> block,
                         size_t offset, size_t size)
    : block_range(block->span(), offset, size) {
  block_ = std::move(block);
}

} // namespace dwarfs::reader
