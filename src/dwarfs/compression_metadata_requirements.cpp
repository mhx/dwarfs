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

#include "dwarfs/compression_metadata_requirements.h"

namespace dwarfs::detail {

void check_dynamic_common(folly::dynamic const& dyn,
                          std::string_view expected_type, size_t expected_size,
                          std::string_view name) {
  if (dyn.type() != folly::dynamic::ARRAY) {
    throw std::runtime_error(
        fmt::format("found non-array type for requirement '{}'", name));
  }
  if (dyn.empty()) {
    throw std::runtime_error(
        fmt::format("unexpected empty value for requirement '{}'", name));
  }
  if (auto type = dyn[0].asString(); type != expected_type) {
    throw std::runtime_error(
        fmt::format("invalid type '{}' for requirement '{}', expected '{}'",
                    type, name, expected_type));
  }
  if (dyn.size() != expected_size) {
    throw std::runtime_error(
        fmt::format("unexpected size '{}' for requirement '{}', expected {}",
                    dyn.size(), name, expected_size));
  }
}

void check_unsupported_metadata_requirements(folly::dynamic& req) {
  if (!req.empty()) {
    std::vector<std::string> keys;
    for (auto k : req.keys()) {
      keys.emplace_back(k.asString());
    }
    std::sort(keys.begin(), keys.end());
    throw std::runtime_error(fmt::format(
        "unsupported metadata requirements: {}", folly::join(", ", keys)));
  }
}

} // namespace dwarfs::detail
