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

#include <cstddef>
#include <iosfwd>

namespace dwarfs::reader {

struct block_cache_options {
  size_t max_bytes{static_cast<size_t>(512) << 20};
  size_t num_workers{0};
  double decompress_ratio{1.0};
  bool mm_release{true};
  bool init_workers{true};
  bool disable_block_integrity_check{false};
  size_t sequential_access_detector_threshold{0};
};

std::ostream& operator<<(std::ostream& os, block_cache_options const& opts);

} // namespace dwarfs::reader
