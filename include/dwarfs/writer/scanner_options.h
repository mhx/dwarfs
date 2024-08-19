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
#include <optional>
#include <string>
#include <vector>

#include <dwarfs/file_stat.h>
#include <dwarfs/history_config.h>
#include <dwarfs/writer/inode_options.h>

namespace dwarfs::writer {

class entry_interface;

struct scanner_options {
  std::optional<std::string> file_hash_algorithm{"xxh3-128"};
  std::optional<file_stat::uid_type> uid;
  std::optional<file_stat::gid_type> gid;
  std::optional<uint64_t> timestamp;
  bool keep_all_times{false};
  bool remove_empty_dirs{false};
  bool with_devices{false};
  bool with_specials{false};
  uint32_t time_resolution_sec{1};
  inode_options inode;
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
  std::optional<std::function<void(bool, writer::entry_interface const&)>>
      debug_filter_function;
  size_t num_segmenter_workers{1};
  bool enable_history{true};
  std::optional<std::vector<std::string>> command_line_arguments;
  history_config history;
  size_t inode_size_cache_min_chunk_count{128};
};

} // namespace dwarfs::writer
