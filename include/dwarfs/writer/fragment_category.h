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
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#include <cassert>
#include <compare>
#include <cstdint>
#include <limits>
#include <ostream>

#include <fmt/format.h>

namespace dwarfs::writer {

class fragment_category {
 public:
  using value_type = uint32_t;

  static constexpr value_type const uninitialized{
      std::numeric_limits<value_type>::max()};
  static constexpr value_type const min{0};
  static constexpr value_type const max{std::numeric_limits<value_type>::max() -
                                        1};

  fragment_category() = default;

  explicit fragment_category(value_type v)
      : value_{v} {}

  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
  fragment_category(value_type v, value_type subcategory)
      : value_{v}
      , subcategory_{subcategory} {}

  fragment_category(fragment_category const&) = default;
  fragment_category(fragment_category&&) = default;

  fragment_category& operator=(fragment_category const&) = default;
  fragment_category& operator=(fragment_category&&) = default;

  fragment_category& operator=(value_type v) {
    assert(v != uninitialized);
    value_ = v;
    return *this;
  }

  value_type value() const {
    assert(!empty());
    return value_;
  }

  void clear() {
    value_ = uninitialized;
    subcategory_ = uninitialized;
  }

  bool empty() const { return value_ == uninitialized; }

  explicit operator bool() const { return !empty(); }

  void set_subcategory(value_type subcategory) {
    assert(!empty());
    assert(subcategory != uninitialized);
    subcategory_ = subcategory;
  }

  bool has_subcategory() const {
    return !empty() && subcategory_ != uninitialized;
  }

  value_type subcategory() const {
    assert(!empty());
    assert(subcategory_ != uninitialized);
    return subcategory_;
  }

  auto operator<=>(fragment_category const&) const = default;

  size_t hash() const;

 private:
  value_type value_{uninitialized};
  value_type subcategory_{uninitialized};
};

inline std::ostream&
operator<<(std::ostream& os, fragment_category const& cat) {
  if (cat) {
    os << cat.value();

    if (cat.has_subcategory()) {
      os << '.' << cat.subcategory();
    }
  } else {
    os << "uninitialized";
  }

  return os;
}

} // namespace dwarfs::writer

template <>
struct fmt::formatter<dwarfs::writer::fragment_category>
    : formatter<std::string> {
  template <typename FormatContext>
  auto format(dwarfs::writer::fragment_category const& cat,
              FormatContext& ctx) const {
    if (cat) {
      if (cat.has_subcategory()) {
        return formatter<std::string>::format(
            fmt::format("{}.{}", cat.value(), cat.subcategory()), ctx);
      }
      return formatter<std::string>::format(fmt::format("{}", cat.value()),
                                            ctx);
    }
    return formatter<std::string>::format("uninitialized", ctx);
  }
};

namespace std {

template <>
struct hash<dwarfs::writer::fragment_category> {
  std::size_t operator()(dwarfs::writer::fragment_category const& k) const {
    return k.hash();
  }
};

} // namespace std
