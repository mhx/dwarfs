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

#include <dwarfs/error.h>

#include "base.h"

namespace dwarfs {

void block_decompressor_base::start_decompression(mutable_byte_buffer target) {
  DWARFS_CHECK(!decompressed_, "decompression already started");

  decompressed_ = std::move(target);

  auto size = this->uncompressed_size();

  try {
    decompressed_.reserve(size);
  } catch (std::bad_alloc const&) {
    DWARFS_THROW(
        runtime_error,
        fmt::format("could not reserve {} bytes for decompressed block", size));
  }
}

std::optional<std::string> block_decompressor_base::metadata() const {
  return std::nullopt;
}

} // namespace dwarfs
