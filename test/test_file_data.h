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
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <random>
#include <string>
#include <vector>

#include <dwarfs/detail/file_extent_info.h>

namespace dwarfs::test {

struct test_file_extent {
  dwarfs::detail::file_extent_info info;
  std::string data{};
};

struct test_file_extent_spec {
  extent_kind kind;
  file_size_t size{0};
  std::mt19937_64* rng{nullptr};
};

struct test_file_data {
  std::vector<test_file_extent> extents;

  test_file_data() = default;
  explicit(false)
      test_file_data(std::initializer_list<test_file_extent_spec> list);

  void add_data(std::string data);
  void add_data(file_size_t size, std::mt19937_64* rng);
  void add_hole(file_size_t size);

  file_size_t size() const;
  file_size_t allocated_size() const;
};

} // namespace dwarfs::test
