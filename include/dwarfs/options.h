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

#include <chrono>
#include <cstddef>
#include <functional>
#include <iosfwd>
#include <memory>
#include <optional>
#include <unordered_set>
#include <vector>

#include "dwarfs/categorized_option.h"
#include "dwarfs/file_stat.h"
#include "dwarfs/types.h"

namespace dwarfs {

class categorizer_manager;
class entry;

enum class mlock_mode { NONE, TRY, MUST };

enum class cache_tidy_strategy { NONE, EXPIRY_TIME, BLOCK_SWAPPED_OUT };

enum class filesystem_check_level { CHECKSUM, INTEGRITY, FULL };

struct block_cache_options {
  size_t max_bytes{0};
  size_t num_workers{0};
  double decompress_ratio{1.0};
  bool mm_release{true};
  bool init_workers{true};
  bool disable_block_integrity_check{false};
};

struct history_config {
  bool with_timestamps{false};
};

struct cache_tidy_config {
  cache_tidy_strategy strategy{cache_tidy_strategy::NONE};
  std::chrono::milliseconds interval{std::chrono::seconds(1)};
  std::chrono::milliseconds expiry_time{std::chrono::seconds(60)};
};

struct metadata_options {
  bool enable_nlink{false};
  bool readonly{false};
  bool check_consistency{false};
};

struct filesystem_options {
  static constexpr file_off_t IMAGE_OFFSET_AUTO{-1};

  mlock_mode lock_mode{mlock_mode::NONE};
  file_off_t image_offset{0};
  block_cache_options block_cache{};
  metadata_options metadata{};
  int inode_offset{0};
};

struct filesystem_writer_options {
  size_t max_queue_size{64 << 20};
  size_t worst_case_block_size{4 << 20};
  bool remove_header{false};
  bool no_section_index{false};
};

// TODO: rename
enum class file_order_mode { NONE, PATH, REVPATH, SIMILARITY, NILSIMSA };

// TODO: rename
struct file_order_options {
  static constexpr int const kDefaultNilsimsaMaxChildren{16384};
  static constexpr int const kDefaultNilsimsaMaxClusterSize{16384};

  file_order_mode mode{file_order_mode::NONE};
  int nilsimsa_max_children{kDefaultNilsimsaMaxChildren};
  int nilsimsa_max_cluster_size{kDefaultNilsimsaMaxClusterSize};
};

struct inode_options {
  std::optional<size_t> max_similarity_scan_size;
  std::shared_ptr<categorizer_manager> categorizer_mgr;
  categorized_option<file_order_options> fragment_order{file_order_options()};
};

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
  std::optional<std::function<void(bool, entry const*)>> debug_filter_function;
  size_t num_segmenter_workers{1};
  bool enable_history{true};
  std::optional<std::vector<std::string>> command_line_arguments;
  history_config history;
};

struct rewrite_options {
  bool recompress_block{false};
  bool recompress_metadata{false};
  std::unordered_set<std::string> recompress_categories;
  bool recompress_categories_exclude{false};
  bool enable_history{true};
  std::optional<std::vector<std::string>> command_line_arguments;
  history_config history;
};

std::ostream& operator<<(std::ostream& os, file_order_mode mode);
std::ostream& operator<<(std::ostream& os, block_cache_options const& opts);

mlock_mode parse_mlock_mode(std::string_view mode);

} // namespace dwarfs
