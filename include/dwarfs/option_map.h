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

#include <string>
#include <string_view>
#include <unordered_map>

#include <folly/Conv.h>

namespace dwarfs {

class option_map {
 public:
  explicit option_map(std::string_view spec);

  const std::string& choice() const { return choice_; }

  bool has_options() const { return !opt_.empty(); }

  template <typename T>
  T get(const std::string& key, const T& default_value = T()) {
    auto i = opt_.find(key);

    if (i != opt_.end()) {
      std::string val = i->second;
      opt_.erase(i);
      return folly::to<T>(val);
    }

    return default_value;
  }

  size_t get_size(const std::string& key, size_t default_value = 0);

  void report();

 private:
  std::unordered_map<std::string, std::string> opt_;
  std::string choice_;
};

} // namespace dwarfs
