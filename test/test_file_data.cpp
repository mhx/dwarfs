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

#include "test_file_data.h"
#include "loremipsum.h"
#include "test_helpers.h"

namespace dwarfs::test {

test_file_data::test_file_data(
    std::initializer_list<test_file_extent_spec> list) {
  for (auto const& e : list) {
    switch (e.kind) {
    case extent_kind::data:
      add_data(e.size, e.rng);
      break;
    case extent_kind::hole:
      add_hole(e.size);
      break;
    }
  }
}

void test_file_data::add_data(std::string data) {
  file_off_t offset{this->size()};
  extents.push_back({
      .info = {extent_kind::data, file_range{offset, data.size()}},
      .data = std::move(data),
  });
}

void test_file_data::add_data(file_size_t size, std::mt19937_64* rng) {
  auto data = rng ? create_random_string(size, *rng) : loremipsum(size);
  add_data(data);
}

void test_file_data::add_hole(file_size_t size) {
  file_off_t offset{this->size()};
  extents.push_back({.info = {extent_kind::hole, file_range{offset, size}}});
}

file_size_t test_file_data::size() const {
  file_size_t total{0};
  if (!extents.empty()) {
    total = extents.back().info.range.end();
  }
  return total;
}

} // namespace dwarfs::test
