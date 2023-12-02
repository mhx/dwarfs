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
#include <iterator>

#include <thrift/lib/cpp/util/EnumUtils.h>

#include "dwarfs/features.h"

namespace dwarfs {

namespace {

constexpr bool is_supported_feature(feature /*f*/) { return true; }

std::string feature_name(feature f) {
  return apache::thrift::util::enumNameOrThrow(f);
}

} // namespace

void feature_set::add(feature f) { features_.insert(feature_name(f)); }

std::set<std::string> feature_set::get_supported() {
  std::set<std::string> rv;
  for (auto f : apache::thrift::TEnumTraits<feature>::values) {
    if (is_supported_feature(f)) {
      rv.insert(feature_name(f));
    }
  }
  return rv;
};

std::set<std::string>
feature_set::get_unsupported(std::set<std::string> wanted_features) {
  auto const supported_features = get_supported();
  std::set<std::string> missing;
  std::set_difference(wanted_features.begin(), wanted_features.end(),
                      supported_features.begin(), supported_features.end(),
                      std::inserter(missing, missing.end()));
  return missing;
}

} // namespace dwarfs
