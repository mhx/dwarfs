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
#include <string_view>
#include <vector>

namespace dwarfs::internal {

class block_data {
 public:
  block_data() = default;
  explicit block_data(std::vector<uint8_t>&& vec)
      : vec_{std::move(vec)} {}
  explicit block_data(std::string_view str)
      : vec_{str.begin(), str.end()} {}

  std::vector<uint8_t> const& vec() const { return vec_; }
  std::vector<uint8_t>& vec() { return vec_; }

  void reserve(size_t size) { vec_.reserve(size); }

  uint8_t const* data() const { return vec_.data(); }
  uint8_t* data() { return vec_.data(); }

  size_t size() const { return vec_.size(); }

  bool empty() const { return vec_.empty(); }

 private:
  std::vector<uint8_t> vec_;
};

} // namespace dwarfs::internal
