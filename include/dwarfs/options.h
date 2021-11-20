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
#include <iosfwd>
#include <optional>

#include <sys/types.h>

namespace dwarfs {

enum class mlock_mode { NONE, TRY, MUST };

enum class cache_tidy_strategy { NONE, EXPIRY_TIME, BLOCK_SWAPPED_OUT };

struct block_cache_options {
  size_t max_bytes{0};
  size_t num_workers{0};
  double decompress_ratio{1.0};
  bool mm_release{true};
  bool init_workers{true};
};

struct cache_tidy_config {
  cache_tidy_strategy strategy{cache_tidy_strategy::NONE};
  std::chrono::milliseconds interval;
  std::chrono::milliseconds expiry_time;
};

struct metadata_options {
  bool enable_nlink{false};
  bool readonly{false};
  bool check_consistency{false};
};

struct filesystem_options {
  static constexpr off_t IMAGE_OFFSET_AUTO{-1};

  mlock_mode lock_mode{mlock_mode::NONE};
  off_t image_offset{0};
  block_cache_options block_cache;
  metadata_options metadata;
};

struct filesystem_writer_options {
  size_t max_queue_size{64 << 20};
  bool remove_header{false};
  bool no_section_index{false};
};

struct inode_options {
  bool with_similarity{false};
  bool with_nilsimsa{false};

  bool needs_scan() const { return with_similarity || with_nilsimsa; }
};

enum class file_order_mode { NONE, PATH, SCRIPT, SIMILARITY, NILSIMSA };

struct file_order_options {
  file_order_mode mode{file_order_mode::NONE};
  int nilsimsa_depth{20000};
  int nilsimsa_min_depth{1000};
  int nilsimsa_limit{255};
};

struct scanner_options {
  file_order_options file_order;
  std::optional<uint16_t> uid;
  std::optional<uint16_t> gid;
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
};

struct rewrite_options {
  bool recompress_block{false};
  bool recompress_metadata{false};
  off_t image_offset{filesystem_options::IMAGE_OFFSET_AUTO};
};

std::ostream& operator<<(std::ostream& os, file_order_mode mode);

mlock_mode parse_mlock_mode(std::string_view mode);

} // namespace dwarfs
