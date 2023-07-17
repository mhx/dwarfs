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

#include <cstdint>
#include <limits>

namespace dwarfs {

class file_category {
 public:
  using value_type = uint32_t;

  static constexpr value_type const uninitialized{
      std::numeric_limits<value_type>::max()};
  static constexpr value_type const min{0};
  static constexpr value_type const max{std::numeric_limits<value_type>::max() -
                                        1};

  file_category()
      : value_{uninitialized} {}
  file_category(value_type v)
      : value_{v} {}

  file_category(file_category const&) = default;
  file_category(file_category&&) = default;

  file_category& operator=(file_category const&) = default;
  file_category& operator=(file_category&&) = default;

  file_category& operator=(value_type v) {
    value_ = v;
    return *this;
  }

  value_type value() const {
    if (empty()) {
      throw std::range_error("file_category is uninitialized");
    }
    return value_;
  }

  void clear() { value_ = uninitialized; }

  bool empty() const { return value_ == uninitialized; }

  explicit operator bool() const { return !empty(); }

 private:
  value_type value_;
};

} // namespace dwarfs
