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
#include <optional>

namespace dwarfs {

enum class mlock_mode { NONE, TRY, MUST };

struct block_cache_options {
  size_t max_bytes{0};
  size_t num_workers{0};
  double decompress_ratio{1.0};
};

struct filesystem_options {
  mlock_mode lock_mode{mlock_mode::NONE};
  block_cache_options block_cache;
};

enum class file_order_mode { NONE, PATH, SCRIPT, SIMILARITY };

struct scanner_options {
  file_order_mode file_order;
  std::optional<uint16_t> uid;
  std::optional<uint16_t> gid;
  std::optional<uint64_t> timestamp;
};

std::ostream& operator<<(std::ostream& os, file_order_mode mode);

mlock_mode parse_mlock_mode(std::string_view mode);

} // namespace dwarfs
