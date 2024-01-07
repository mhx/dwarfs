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

#include <algorithm>
#include <vector>

#include <fmt/format.h>

#include <folly/String.h>

#include "dwarfs/error.h"
#include "dwarfs/option_map.h"
#include "dwarfs/util.h"

namespace dwarfs {

option_map::option_map(const std::string_view spec) {
  std::vector<std::string_view> arg;
  folly::split(':', spec, arg);

  choice_ = arg[0];

  for (size_t i = 1; i < arg.size(); ++i) {
    std::string key;
    std::string val;

    if (auto eqpos = arg[i].find('='); eqpos != std::string_view::npos) {
      key.assign(arg[i].substr(0, eqpos));
      val.assign(arg[i].substr(eqpos + 1));
    } else {
      key.assign(arg[i]);
      val.assign("1");
    }

    if (!opt_.emplace(key, val).second) {
      DWARFS_THROW(
          runtime_error,
          fmt::format("duplicate option {} for choice {}", key, choice_));
    }
  }
}

size_t option_map::get_size(const std::string& key, size_t default_value) {
  auto i = opt_.find(key);

  if (i != opt_.end()) {
    std::string val = i->second;
    opt_.erase(i);
    return parse_size_with_unit(val);
  }

  return default_value;
}

void option_map::report() {
  if (!opt_.empty()) {
    std::vector<std::string> invalid;
    std::transform(opt_.begin(), opt_.end(), std::back_inserter(invalid),
                   [](const auto& p) { return p.first; });
    std::sort(invalid.begin(), invalid.end());
    DWARFS_THROW(runtime_error,
                 fmt::format("invalid option(s) for choice {}: {}", choice_,
                             fmt::join(invalid, ", ")));
  }
}

} // namespace dwarfs
