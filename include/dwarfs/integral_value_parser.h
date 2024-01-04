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

#include <functional>
#include <initializer_list>
#include <set>
#include <string>
#include <string_view>
#include <variant>

#include <fmt/format.h>

#include <folly/Conv.h>
#include <folly/String.h>

#include "dwarfs/overloaded.h"

namespace dwarfs {

template <typename T>
class integral_value_parser {
 public:
  integral_value_parser() = default;
  integral_value_parser(T min, T max)
      : valid_{std::in_place_type<std::pair<T, T>>, min, max} {}
  integral_value_parser(std::initializer_list<T> choices)
      : valid_{std::in_place_type<std::set<T>>, choices} {}
  integral_value_parser(std::function<bool(T)> check)
      : valid_{std::in_place_type<std::function<bool(T)>>, check} {}

  T parse(std::string_view arg) const {
    auto val = folly::to<T>(arg);

    std::visit(overloaded{
                   [](std::monostate const&) {},
                   [val](std::pair<T, T> const& minmax) {
                     if (val < minmax.first || val > minmax.second) {
                       throw std::range_error(
                           fmt::format("value {} out of range [{}..{}]", val,
                                       minmax.first, minmax.second));
                     }
                   },
                   [val](std::set<T> const& choices) {
                     if (auto it = choices.find(val); it == choices.end()) {
                       throw std::range_error(
                           fmt::format("invalid value {}, must be one of [{}]",
                                       val, folly::join(", ", choices)));
                     }
                   },
                   [val](std::function<bool(T)> const& check) {
                     if (!check(val)) {
                       throw std::range_error(
                           fmt::format("value {} out of range", val));
                     }
                   },
               },
               valid_);

    return val;
  }

  std::string to_string(T const& val) const {
    return folly::to<std::string>(val);
  }

 private:
  std::variant<std::monostate, std::pair<T, T>, std::set<T>,
               std::function<bool(T)>>
      valid_;
};

} // namespace dwarfs
