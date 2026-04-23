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

#include <algorithm>
#include <cassert>
#include <concepts>
#include <functional>
#include <numeric>
#include <ostream>
#include <sstream>

#include <dwarfs/error.h>
#include <dwarfs/util.h>
#include <dwarfs/writer/scanner_options.h>

#include <dwarfs/writer/internal/global_entry_data.h>
#include <dwarfs/writer/internal/time_resolution_converter.h>

#include <dwarfs/gen-cpp-lite/metadata_types.h>

namespace dwarfs::writer::internal {

namespace {

template <typename T>
concept unsigned_mapped_map = requires {
  typename T::key_type;
  typename T::mapped_type;
} && std::unsigned_integral<typename T::mapped_type>;

template <unsigned_mapped_map T>
  requires std::totally_ordered<typename T::key_type>
void sort_and_index_map(T& map) {
  using index_type = T::mapped_type;
  using iterator = T::iterator;

  std::vector<iterator> order;
  order.reserve(map.size());

  for (auto it = map.begin(); it != map.end(); ++it) {
    order.push_back(it);
  }

  std::ranges::sort(order, {},
                    [](iterator it) -> auto const& { return it->first; });

  index_type ix{0};

  for (iterator it : order) {
    it->second = ix++;
  }
}

template <typename OutT = void, unsigned_mapped_map MapT>
  requires std::same_as<OutT, void> ||
           std::constructible_from<OutT, typename MapT::key_type const&>
auto get_sorted_index_vector(MapT const& map)
    -> std::vector<std::conditional_t<std::same_as<OutT, void>,
                                      typename MapT::key_type, OutT>> {
  using Elem = std::conditional_t<std::same_as<OutT, void>,
                                  typename MapT::key_type, OutT>;

  std::vector<Elem> result(map.size());
#ifndef NDEBUG
  std::vector<bool> seen(result.size(), false);
#endif

  for (auto const& [k, ix] : map) {
    auto const i = static_cast<std::size_t>(ix);
    assert(i < result.size());
    result[i] = k;
#ifndef NDEBUG
    assert(!seen[i]);
    seen[i] = true;
#endif
  }

  assert(std::ranges::all_of(seen, std::identity{}));

  return result;
}

template <typename T, typename V>
  requires requires(T t) {
    typename T::key_type;
    typename T::mapped_type;
  } && std::convertible_to<V, typename T::key_type>
void add_to_index(T& map, V val) {
  auto const next_index = static_cast<typename T::mapped_type>(map.size());
  map.emplace(val, next_index);
}

} // namespace

global_entry_data::global_entry_data(metadata_options const& options)
    : options_{options} {}

auto global_entry_data::get_uids() const -> std::vector<uid_type> {
  return get_sorted_index_vector(uids_);
}

auto global_entry_data::get_gids() const -> std::vector<gid_type> {
  return get_sorted_index_vector(gids_);
}

auto global_entry_data::get_modes() const -> std::vector<mode_type> {
  return get_sorted_index_vector(modes_);
}

auto global_entry_data::get_names() const -> std::vector<std::string> {
  return get_sorted_index_vector<std::string>(names_);
}

auto global_entry_data::get_symlinks() const -> std::vector<std::string> {
  return get_sorted_index_vector<std::string>(symlinks_);
}

void global_entry_data::update_index() {
  sort_and_index_map(names_);
  sort_and_index_map(symlinks_);
}

uint64_t global_entry_data::get_timestamp_base() const {
  return options_.timestamp ? *options_.timestamp : timestamp_base_;
}

void global_entry_data::pack_inode_stat(
    thrift::metadata::metadata::inodes_member_type::reference inode,
    file_stat const& stat, time_resolution_converter const& timeres) const {
  stat.ensure_valid(file_stat::uid_valid | file_stat::gid_valid |
                    file_stat::mode_valid | file_stat::atime_valid |
                    file_stat::mtime_valid | file_stat::ctime_valid);

  inode.mode_index() = DWARFS_NOTHROW(modes_.at(stat.mode_unchecked()));
  inode.owner_index() =
      options_.uid ? 0 : DWARFS_NOTHROW(uids_.at(stat.uid_unchecked()));
  inode.group_index() =
      options_.gid ? 0 : DWARFS_NOTHROW(gids_.at(stat.gid_unchecked()));

  if (!options_.timestamp) {
    auto const base = timeres.align_offset(timestamp_base_);

    {
      auto const mts = stat.mtimespec_unchecked();
      inode.mtime_offset() = timeres.convert_offset(mts.sec - base);
      inode.mtime_subsec() = timeres.convert_subsec(mts.nsec);
    }

    if (options_.keep_all_times) {
      {
        auto const ats = stat.atimespec_unchecked();
        inode.atime_offset() = timeres.convert_offset(ats.sec - base);
        inode.atime_subsec() = timeres.convert_subsec(ats.nsec);
      }

      {
        auto const cts = stat.ctimespec_unchecked();
        inode.ctime_offset() = timeres.convert_offset(cts.sec - base);
        inode.ctime_subsec() = timeres.convert_subsec(cts.nsec);
      }
    }
  }
}

uint32_t global_entry_data::get_name_index(std::string_view name) const {
  return DWARFS_NOTHROW(names_.at(name));
}

uint32_t
global_entry_data::get_symlink_table_entry(std::string_view link) const {
  return DWARFS_NOTHROW(symlinks_.at(link));
}

void global_entry_data::add_uid(uid_type uid) {
  if (!options_.uid) {
    add_to_index(uids_, uid);
  }
}

void global_entry_data::add_gid(gid_type gid) {
  if (!options_.gid) {
    add_to_index(gids_, gid);
  }
}

void global_entry_data::add_mode(mode_type mode) { add_to_index(modes_, mode); }

void global_entry_data::add_mtime(uint64_t time) {
  timestamp_base_ = std::min(time, timestamp_base_);
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

void global_entry_data::add_name(std::string_view name) {
  names_.emplace(name, 0);
}

void global_entry_data::add_link(std::string_view link) {
  symlinks_.emplace(link, 0);
}

void global_entry_data::dump(std::ostream& os) const {
  std::vector<std::pair<std::string_view, std::size_t>> sizes;

  sizes.emplace_back("uids",
                     uids_.capacity() * sizeof(decltype(uids_)::value_type));
  sizes.emplace_back("gids",
                     gids_.capacity() * sizeof(decltype(gids_)::value_type));
  sizes.emplace_back("modes",
                     modes_.capacity() * sizeof(decltype(modes_)::value_type));
  sizes.emplace_back("names",
                     names_.capacity() * sizeof(decltype(names_)::value_type));
  sizes.emplace_back("symlinks", symlinks_.capacity() *
                                     sizeof(decltype(symlinks_)::value_type));

  auto const total_bytes = std::accumulate(
      sizes.begin(), sizes.end(), 0ULL,
      [](std::size_t acc, auto const& pair) { return acc + pair.second; });

  os << "Global Entry Data (" << size_with_unit(total_bytes) << "):\n";

  for (auto const& [label, bytes] : sizes) {
    if (bytes > 0) {
      os << "  " << label << ": " << size_with_unit(bytes) << "\n";
    }
  }
}

std::string global_entry_data::to_string() const {
  std::ostringstream oss;
  dump(oss);
  return oss.str();
}

} // namespace dwarfs::writer::internal
