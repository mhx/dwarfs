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

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/split.hpp>

#include "dwarfs/error.h"
#include "dwarfs/option_map.h"

namespace dwarfs {

option_map::option_map(const std::string_view spec) {
  std::vector<std::string_view> arg;
  boost::split(arg, spec, boost::is_any_of(":"));

  choice_ = arg[0];

  for (size_t i = 1; i < arg.size(); ++i) {
    std::vector<std::string> kv;
    boost::split(kv, arg[i], boost::is_any_of("="));

    if (kv.size() > 2) {
      DWARFS_THROW(runtime_error,
                   "error parsing option " + kv[0] + " for choice " + choice_);
    }

    opt_[kv[0]] = kv.size() > 1 ? kv[1] : std::string("1");
  }
}

void option_map::report() {
  if (!opt_.empty()) {
    std::vector<std::string> invalid;
    std::transform(
        opt_.begin(), opt_.end(), std::back_inserter(invalid),
        [](const std::pair<std::string, std::string>& p) { return p.first; });
    DWARFS_THROW(runtime_error, "invalid option(s) for choice " + choice_ +
                                    ": " + boost::join(invalid, ", "));
  }
}

} // namespace dwarfs
