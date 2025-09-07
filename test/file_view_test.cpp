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

#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <dwarfs/file_view.h>

#include "mmap_mock.h"

using namespace dwarfs;
using namespace std::string_literals;

TEST(file_view, mock_file_view) {
  auto view = test::make_mock_file_view("Hello, World!");

  {
    std::vector<std::string> parts;

    for (auto const& ext : view.extents()) {
      for (auto const& seg : ext.segments(4)) {
        auto span = seg.span<char>();
        parts.emplace_back(span.begin(), span.end());
      }
    }

    EXPECT_THAT(parts, testing::ElementsAre("Hell", "o, W", "orld", "!"));
  }

  {
    std::vector<std::string> parts;

    for (auto const& ext : view.extents()) {
      for (auto const& seg : ext.segments(4, 1)) {
        auto span = seg.span<char>();
        parts.emplace_back(span.begin(), span.end());
      }
    }

    EXPECT_THAT(parts, testing::ElementsAre("Hell", "lo, ", " Wor", "rld!"));
  }

  {
    std::vector<std::string> parts;

    for (auto const& seg : view.segments({2, 11}, 4, 1)) {
      auto span = seg.span<char>();
      parts.emplace_back(span.begin(), span.end());
    }

    EXPECT_THAT(parts, testing::ElementsAre("llo,", ", Wo", "orl"));
  }
}
