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
#include <vector>

#include <parallel_hashmap/phmap.h>

#include <dwarfs/file_stat.h>

namespace dwarfs::thrift::metadata {
class inode_data;
}

namespace dwarfs::writer {

struct metadata_options;

namespace internal {

class global_entry_data {
 public:
  using uid_type = file_stat::uid_type;
  using gid_type = file_stat::gid_type;
  using mode_type = file_stat::mode_type;

  enum class timestamp_type { ATIME, MTIME, CTIME };

  global_entry_data(metadata_options const& options)
      : options_{options} {}

  void add_uid(uid_type uid);
  void add_gid(gid_type gid);

  void add_mode(mode_type mode) { add(mode, modes_, next_mode_index_); }

  void add_mtime(uint64_t time);
  void add_atime(uint64_t time);
  void add_ctime(uint64_t time);

  void add_name(std::string const& name) { names_.emplace(name, 0); }
  void add_link(std::string const& link) { symlinks_.emplace(link, 0); }

  void index();

  uint32_t get_name_index(std::string const& name) const;
  uint32_t get_symlink_table_entry(std::string const& link) const;

  std::vector<uid_type> get_uids() const;
  std::vector<gid_type> get_gids() const;
  std::vector<mode_type> get_modes() const;

  std::vector<std::string> get_names() const;
  std::vector<std::string> get_symlinks() const;

  uint64_t get_timestamp_base() const;

  void pack_inode_stat(thrift::metadata::inode_data& inode,
                       file_stat const& stat) const;

 private:
  template <typename K, typename V>
  using map_type = phmap::flat_hash_map<K, V>;

  template <typename T, typename U>
  std::vector<T> get_vector(map_type<T, U> const& map) const;

  template <typename T>
  void add(T val, map_type<T, T>& map, T& next_index) {
    if (map.emplace(val, next_index).second) {
      ++next_index;
    }
  }

  uint64_t get_time_offset(uint64_t time) const;
  uint32_t get_time_subsec(uint32_t nsec) const;

  map_type<uid_type, uid_type> uids_;
  map_type<gid_type, gid_type> gids_;
  map_type<mode_type, mode_type> modes_;
  map_type<std::string, uint32_t> names_;
  map_type<std::string, uint32_t> symlinks_;
  uid_type next_uid_index_{0};
  gid_type next_gid_index_{0};
  mode_type next_mode_index_{0};
  uint64_t timestamp_base_{std::numeric_limits<uint64_t>::max()};
  metadata_options const& options_;
};

} // namespace internal
} // namespace dwarfs::writer
