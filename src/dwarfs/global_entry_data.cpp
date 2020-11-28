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
  return get_vector(uids);
}

std::vector<uint16_t> global_entry_data::get_gids() const {
  return get_vector(gids);
}

std::vector<uint16_t> global_entry_data::get_modes() const {
  return get_vector(modes);
}

std::vector<std::string> global_entry_data::get_names() const {
  return get_vector(names);
}

std::vector<std::string> global_entry_data::get_links() const {
  return get_vector(links);
}

void global_entry_data::index(std::unordered_map<std::string, uint32_t>& map) {
  using namespace folly::gen;
  uint32_t ix = 0;
  from(map) | get<0>() | order | [&](std::string const& s) { map[s] = ix++; };
}

} // namespace dwarfs
