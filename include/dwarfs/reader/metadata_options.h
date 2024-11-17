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
#include <optional>

#include <dwarfs/file_stat.h>

namespace dwarfs::reader {

struct metadata_options {
  bool enable_nlink{false};
  bool readonly{false};
  bool check_consistency{false};
  bool enable_case_insensitive_lookup{true};
  size_t block_size{512};
  std::optional<file_stat::uid_type> fs_uid{};
  std::optional<file_stat::gid_type> fs_gid{};
};

} // namespace dwarfs::reader
