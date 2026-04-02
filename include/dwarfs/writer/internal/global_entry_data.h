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
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include <parallel_hashmap/phmap.h>

#include <dwarfs/detail/string_like_hash.h>
#include <dwarfs/file_stat.h>

namespace dwarfs::thrift::metadata {
class inode_data;
}

namespace dwarfs::writer {

struct metadata_options;

namespace internal {

class time_resolution_converter;

class global_entry_data {
 public:
  using uid_type = file_stat::uid_type;
  using gid_type = file_stat::gid_type;
  using mode_type = file_stat::mode_type;
  using index_type = uint32_t;

  enum class timestamp_type { ATIME, MTIME, CTIME };

  explicit global_entry_data(metadata_options const& options);

  void add_uid(uid_type uid);
  void add_gid(gid_type gid);

  void add_mode(mode_type mode);

  void add_mtime(uint64_t time);
  void add_atime(uint64_t time);
  void add_ctime(uint64_t time);

  void add_name(std::string_view name);
  void add_link(std::string_view link);

  void index();

  uint32_t get_name_index(std::string_view name) const;
  uint32_t get_symlink_table_entry(std::string_view link) const;

  std::vector<uid_type> get_uids() const;
  std::vector<gid_type> get_gids() const;
  std::vector<mode_type> get_modes() const;

  std::vector<std::string> get_names() const;
  std::vector<std::string> get_symlinks() const;

  uint64_t get_timestamp_base() const;

  void
  pack_inode_stat(thrift::metadata::inode_data& inode, file_stat const& stat,
                  time_resolution_converter const& timeres) const;

 private:
  using string_like_hash = dwarfs::detail::string_like_hash;

  template <typename... Args>
  using map_type = phmap::flat_hash_map<Args...>;

  template <typename T>
  using index_map_type = map_type<T, index_type>;

  template <typename T>
  using string_keyed_map_type =
      map_type<std::string, T, string_like_hash, std::equal_to<>>;

  using string_index_map_type = string_keyed_map_type<index_type>;

  index_map_type<uid_type> uids_;
  index_map_type<gid_type> gids_;
  index_map_type<mode_type> modes_;
  string_index_map_type names_;
  string_index_map_type symlinks_;
  uint64_t timestamp_base_{std::numeric_limits<uint64_t>::max()};
  metadata_options const& options_;
};

} // namespace internal
} // namespace dwarfs::writer
