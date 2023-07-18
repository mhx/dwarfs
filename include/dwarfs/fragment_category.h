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

#include <cassert>
#include <cstdint>
#include <limits>

namespace dwarfs {

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

 private:
  value_type value_{uninitialized};
  value_type subcategory_{uninitialized};
};

} // namespace dwarfs
