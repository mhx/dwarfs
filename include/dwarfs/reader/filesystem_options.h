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

#include <limits>

#include <dwarfs/reader/block_cache_options.h>
#include <dwarfs/reader/inode_reader_options.h>
#include <dwarfs/reader/metadata_options.h>
#include <dwarfs/reader/mlock_mode.h>
#include <dwarfs/types.h>

namespace dwarfs::reader {

struct filesystem_options {
  static constexpr file_off_t IMAGE_OFFSET_AUTO{-1};

  mlock_mode lock_mode{mlock_mode::NONE};
  file_off_t image_offset{0};
  file_off_t image_size{std::numeric_limits<file_off_t>::max()};
  block_cache_options block_cache{};
  metadata_options metadata{};
  inode_reader_options inode_reader{};
  int inode_offset{0};
};

file_off_t parse_image_offset(std::string const& str);

} // namespace dwarfs::reader
