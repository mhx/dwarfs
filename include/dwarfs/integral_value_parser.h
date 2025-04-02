/* vim:set ts=2 sw=2 sts=2 et: */
/**
 * \author     Marcus Holland-Moritz (github@mhxnet.de)
 * \copyright  Copyright (c) Marcus Holland-Moritz
 *
 * This file is part of dwarfs.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the “Software”), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <functional>
#include <initializer_list>
#include <set>
#include <string>
#include <string_view>
#include <variant>

#include <fmt/format.h>
#if FMT_VERSION >= 110000
#include <fmt/ranges.h>
#endif

#include <dwarfs/conv.h>
#include <dwarfs/match.h>

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
    auto val = to<T>(arg);

    valid_ | match{
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
                                     val, fmt::join(choices, ", ")));
                   }
                 },
                 [val](std::function<bool(T)> const& check) {
                   if (!check(val)) {
                     throw std::range_error(
                         fmt::format("value {} out of range", val));
                   }
                 },
             };

    return val;
  }

  std::string to_string(T const& val) const { return to<std::string>(val); }

 private:
  std::variant<std::monostate, std::pair<T, T>, std::set<T>,
               std::function<bool(T)>>
      valid_;
};

} // namespace dwarfs
