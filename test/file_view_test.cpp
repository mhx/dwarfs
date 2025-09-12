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

TEST(file_view, mock_file_view_basic) {
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

    for (auto const& seg : view.segments({2, 9}, 4, 1)) {
      auto span = seg.span<char>();
      parts.emplace_back(span.begin(), span.end());
    }

    EXPECT_THAT(parts, testing::ElementsAre("llo,", ", Wo", "orl"));
  }

  ASSERT_TRUE(view.supports_raw_bytes());

  auto raw = view.raw_bytes();
  std::string raw_str(raw.size(), '\0');
  std::ranges::transform(raw, raw_str.begin(),
                         [](auto b) { return static_cast<char>(b); });

  EXPECT_EQ(raw_str, "Hello, World!");
}

TEST(file_view, mock_file_view_extents) {
  auto view = test::make_mock_file_view("Hello,\0\0\0\0World!"s,
                                        {
                                            {extent_kind::data, {0, 6}},
                                            {extent_kind::hole, {6, 4}},
                                            {extent_kind::data, {10, 6}},
                                        });

  {
    std::vector<std::vector<std::string>> extent_parts;

    for (auto const& ext : view.extents()) {
      auto& parts = extent_parts.emplace_back();
      for (auto const& seg : ext.segments(3)) {
        auto span = seg.span<char>();
        parts.emplace_back(span.begin(), span.end());
      }
    }

    EXPECT_THAT(extent_parts,
                testing::ElementsAre(testing::ElementsAre("Hel"s, "lo,"s),
                                     testing::ElementsAre("\0\0\0"s, "\0"s),
                                     testing::ElementsAre("Wor"s, "ld!"s)));
  }

  {
    std::vector<std::vector<std::string>> extent_parts;

    for (auto const& ext : view.extents({4, 10})) {
      auto& parts = extent_parts.emplace_back();
      for (auto const& seg : ext.segments(3)) {
        auto span = seg.span<char>();
        parts.emplace_back(span.begin(), span.end());
      }
    }

    EXPECT_THAT(extent_parts,
                testing::ElementsAre(testing::ElementsAre("o,"s),
                                     testing::ElementsAre("\0\0\0"s, "\0"s),
                                     testing::ElementsAre("Wor"s, "l"s)));
  }

  {
    std::vector<std::vector<std::string>> extent_parts;

    for (auto const& ext : view.extents({1, 4})) {
      auto& parts = extent_parts.emplace_back();
      for (auto const& seg : ext.segments(3)) {
        auto span = seg.span<char>();
        parts.emplace_back(span.begin(), span.end());
      }
    }

    EXPECT_THAT(extent_parts,
                testing::ElementsAre(testing::ElementsAre("ell"s, "o"s)));
  }

  {
    std::vector<std::vector<std::string>> extent_parts;

    for (auto const& ext : view.extents({9, 2})) {
      auto& parts = extent_parts.emplace_back();
      for (auto const& seg : ext.segments(3)) {
        auto span = seg.span<char>();
        parts.emplace_back(span.begin(), span.end());
      }
    }

    EXPECT_THAT(extent_parts, testing::ElementsAre(testing::ElementsAre("\0"s),
                                                   testing::ElementsAre("W"s)));
  }

  {
    std::vector<std::vector<std::string>> extent_parts;

    for (auto const& ext : view.extents({2, 4})) {
      auto& parts = extent_parts.emplace_back();
      for (auto const& seg : ext.segments(3, 1)) {
        auto span = seg.span<char>();
        parts.emplace_back(span.begin(), span.end());
      }
    }

    EXPECT_THAT(extent_parts,
                testing::ElementsAre(testing::ElementsAre("llo"s, "o,"s)));
  }

  {
    std::vector<std::string> extents;

    for (auto const& ext : view.extents({2, 11})) {
      auto raw = ext.raw_bytes();
      auto& dest = extents.emplace_back();
      dest.resize(raw.size());
      std::ranges::transform(raw, dest.begin(),
                             [](auto b) { return static_cast<char>(b); });
    }

    EXPECT_THAT(extents, testing::ElementsAre("llo,"s, "\0\0\0\0"s, "Wor"s));
  }
}
