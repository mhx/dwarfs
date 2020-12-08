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

#include <folly/gen/Base.h>

#include "dwarfs/global_entry_data.h"
#include "dwarfs/options.h"

namespace dwarfs {

template <typename T, typename U>
std::vector<T>
global_entry_data::get_vector(std::unordered_map<T, U> const& map) const {
  using namespace folly::gen;
  std::vector<std::pair<T, U>> pairs(map.begin(), map.end());
  return from(pairs) | orderBy([](auto const& p) { return p.second; }) |
         get<0>() | as<std::vector>();
}

std::vector<uint16_t> global_entry_data::get_uids() const {
  return get_vector(uids_);
}

std::vector<uint16_t> global_entry_data::get_gids() const {
  return get_vector(gids_);
}

std::vector<uint16_t> global_entry_data::get_modes() const {
  return get_vector(modes_);
}

std::vector<std::string> global_entry_data::get_names() const {
  return get_vector(names_);
}

std::vector<std::string> global_entry_data::get_links() const {
  return get_vector(links_);
}

void global_entry_data::index(std::unordered_map<std::string, uint32_t>& map) {
  using namespace folly::gen;
  uint32_t ix = 0;
  from(map) | get<0>() | order | [&](std::string const& s) { map[s] = ix++; };
}

uint64_t global_entry_data::get_time_offset(uint64_t time) const {
  return (time - timestamp_base_) / options_.time_resolution_sec;
}

uint64_t global_entry_data::get_mtime_offset(uint64_t time) const {
  return !options_.timestamp ? get_time_offset(time) : UINT64_C(0);
}

uint64_t global_entry_data::get_atime_offset(uint64_t time) const {
  return !options_.timestamp && options_.keep_all_times ? get_time_offset(time)
                                                        : UINT64_C(0);
}

uint64_t global_entry_data::get_ctime_offset(uint64_t time) const {
  return !options_.timestamp && options_.keep_all_times ? get_time_offset(time)
                                                        : UINT64_C(0);
}

uint64_t global_entry_data::get_timestamp_base() const {
  return (options_.timestamp ? *options_.timestamp : timestamp_base_) /
         options_.time_resolution_sec;
}

uint16_t global_entry_data::get_uid_index(uint16_t uid) const {
  return options_.uid ? *options_.uid : uids_.at(uid);
}

uint16_t global_entry_data::get_gid_index(uint16_t gid) const {
  return options_.gid ? *options_.gid : gids_.at(gid);
}

void global_entry_data::add_uid(uint16_t uid) {
  if (!options_.uid) {
    add(uid, uids_, next_uid_index_);
  }
}

void global_entry_data::add_gid(uint16_t gid) {
  if (!options_.gid) {
    add(gid, gids_, next_gid_index_);
  }
}

void global_entry_data::add_mtime(uint64_t time) {
  if (time < timestamp_base_) {
    timestamp_base_ = time;
  }
}

void global_entry_data::add_atime(uint64_t time) {
  if (options_.keep_all_times) {
    add_mtime(time);
  }
}

void global_entry_data::add_ctime(uint64_t time) {
  if (options_.keep_all_times) {
    add_mtime(time);
  }
}

} // namespace dwarfs
