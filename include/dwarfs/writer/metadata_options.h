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
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <optional>
#include <string_view>

#include <dwarfs/file_stat.h>
#include <dwarfs/history_config.h>
#include <dwarfs/writer/inode_options.h>

namespace dwarfs::writer {

class entry_interface;

struct metadata_options {
  std::optional<file_stat::uid_type> uid{};
  std::optional<file_stat::gid_type> gid{};
  std::optional<uint64_t> timestamp{};
  bool keep_all_times{false};
  std::optional<uint32_t> time_resolution_sec{};
  std::optional<uint32_t> subsecond_resolution_nsec_multiplier{};
  std::string_view chmod_specifiers{};
  file_stat::mode_type umask{0};
  bool pack_chunk_table{false};
  bool pack_directories{false};
  bool pack_shared_files_table{false};
  bool plain_names_table{false};
  bool pack_names{false};
  bool pack_names_index{false};
  bool plain_symlinks_table{false};
  bool pack_symlinks{false};
  bool pack_symlinks_index{false};
  bool force_pack_string_tables{false};
  bool no_create_timestamp{false};
  bool no_category_names{false};
  bool no_category_metadata{false};
  bool no_metadata_version_history{false};
  bool enable_sparse_files{false};
  bool no_hardlink_table{false};
  size_t inode_size_cache_min_chunk_count{128};

  static void validate(metadata_options const& opts);
};

std::ostream& operator<<(std::ostream& os, metadata_options const& opts);

} // namespace dwarfs::writer
