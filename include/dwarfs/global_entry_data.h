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
#include <string>
#include <unordered_map>
#include <vector>

namespace dwarfs {

class global_entry_data {
 public:
  global_entry_data(bool no_time)
      : no_time_(no_time) {}

  void add_uid(uint16_t uid) { add(uid, uids_, next_uid_index_); }
  void add_gid(uint16_t gid) { add(gid, gids_, next_gid_index_); }
  void add_mode(uint16_t mode) { add(mode, modes_, next_mode_index_); }

  void add_time(uint64_t time) {
    if (time < timestamp_base_) {
      timestamp_base_ = time;
    }
  }

  void add_name(std::string const& name) { names_.emplace(name, 0); }
  void add_link(std::string const& link) { links_.emplace(link, 0); }

  void index() {
    index(names_);
    index(links_);
  }

  uint16_t get_uid_index(uint16_t uid) const { return uids_.at(uid); }
  uint16_t get_gid_index(uint16_t gid) const { return gids_.at(gid); }
  uint16_t get_mode_index(uint16_t mode) const { return modes_.at(mode); }

  uint32_t get_name_index(std::string const& name) const {
    return names_.at(name);
  }

  uint32_t get_link_index(std::string const& link) const {
    return links_.at(link);
  }

  uint64_t get_time_offset(uint64_t time) const {
    return no_time_ ? 0 : time - timestamp_base_;
  }

  std::vector<uint16_t> get_uids() const;
  std::vector<uint16_t> get_gids() const;
  std::vector<uint16_t> get_modes() const;

  std::vector<std::string> get_names() const;
  std::vector<std::string> get_links() const;

  uint64_t get_timestamp_base() const { return timestamp_base_; }

 private:
  template <typename T, typename U>
  std::vector<T> get_vector(std::unordered_map<T, U> const& map) const;

  static void index(std::unordered_map<std::string, uint32_t>& map);

  void add(uint16_t val, std::unordered_map<uint16_t, uint16_t>& map,
           uint16_t& next_index) {
    if (map.emplace(val, next_index).second) {
      ++next_index;
    }
  }

  std::unordered_map<uint16_t, uint16_t> uids_;
  std::unordered_map<uint16_t, uint16_t> gids_;
  std::unordered_map<uint16_t, uint16_t> modes_;
  std::unordered_map<std::string, uint32_t> names_;
  std::unordered_map<std::string, uint32_t> links_;
  uint16_t next_uid_index_{0};
  uint16_t next_gid_index_{0};
  uint16_t next_mode_index_{0};
  uint64_t timestamp_base_{std::numeric_limits<uint64_t>::max()};
  bool no_time_;
};

} // namespace dwarfs
