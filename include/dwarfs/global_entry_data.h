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

#include <cstdint>
#include <limits>
#include <unordered_map>
#include <vector>

namespace dwarfs {

// TODO: clean up
class global_entry_data {
 public:
  global_entry_data(bool no_time)
      : no_time_(no_time) {}

  void add_uid(uint16_t uid) { add(uid, uids, next_uid_index); }

  void add_gid(uint16_t gid) { add(gid, gids, next_gid_index); }

  void add_mode(uint16_t mode) { add(mode, modes, next_mode_index); }

  void add(uint16_t val, std::unordered_map<uint16_t, uint16_t>& map,
           uint16_t& next_index) {
    if (map.emplace(val, next_index).second) {
      ++next_index;
    }
  }

  void add_time(uint64_t time) {
    if (time < timestamp_base) {
      timestamp_base = time;
    }
  }

  void add_name(std::string const& name) { names.emplace(name, 0); }

  void add_link(std::string const& link) { links.emplace(link, 0); }

  void index() {
    index(names);
    index(links);
  }

  void index(std::unordered_map<std::string, uint32_t>& map);

  uint16_t get_uid_index(uint16_t uid) const { return uids.at(uid); }

  uint16_t get_gid_index(uint16_t gid) const { return gids.at(gid); }

  uint16_t get_mode_index(uint16_t mode) const { return modes.at(mode); }

  uint32_t get_name_index(std::string const& name) const {
    return names.at(name);
  }

  uint32_t get_link_index(std::string const& link) const {
    return links.at(link);
  }

  uint64_t get_time_offset(uint64_t time) const {
    return no_time_ ? 0 : time - timestamp_base;
  }

  std::vector<uint16_t> get_uids() const;

  std::vector<uint16_t> get_gids() const;

  std::vector<uint16_t> get_modes() const;

  std::vector<std::string> get_names() const;

  std::vector<std::string> get_links() const;

  // TODO: make private
  template <typename T, typename U>
  std::vector<T> get_vector(std::unordered_map<T, U> const& map) const;

  std::unordered_map<uint16_t, uint16_t> uids;
  std::unordered_map<uint16_t, uint16_t> gids;
  std::unordered_map<uint16_t, uint16_t> modes;
  std::unordered_map<std::string, uint32_t> names;
  std::unordered_map<std::string, uint32_t> links;
  uint16_t next_uid_index{0};
  uint16_t next_gid_index{0};
  uint16_t next_mode_index{0};
  uint64_t timestamp_base{std::numeric_limits<uint64_t>::max()};
  bool no_time_;
};

} // namespace dwarfs
