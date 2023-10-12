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

#include <fmt/format.h>

#include "dwarfs/block_range.h"
#include "dwarfs/cached_block.h"
#include "dwarfs/error.h"

namespace dwarfs {

block_range::block_range(uint8_t const* data, size_t offset, size_t size)
    : span_{data + offset, size} {
  if (!data) {
    DWARFS_THROW(runtime_error, "block_range: block data is null");
  }
}

block_range::block_range(std::shared_ptr<cached_block const> block,
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

} // namespace dwarfs
