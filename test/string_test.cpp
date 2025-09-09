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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <dwarfs/string.h>

using namespace dwarfs;

TEST(string, split_to) {
  EXPECT_THAT(split_to<std::vector<std::string>>("a,b,c", ','),
              testing::ElementsAre("a", "b", "c"));
  EXPECT_THAT(split_to<std::vector<std::string>>(",b,", ','),
              testing::ElementsAre("", "b", ""));
  EXPECT_THAT(split_to<std::vector<std::string>>(",,", ','),
              testing::ElementsAre("", "", ""));
  EXPECT_THAT(split_to<std::vector<std::string>>("", ','),
              testing::ElementsAre());
  EXPECT_THAT(split_to<std::vector<std::string_view>>("a,,c", ','),
              testing::ElementsAre("a", "", "c"));
  EXPECT_THAT(split_to<std::vector<std::string>>("aa\nbb\nccc", '\n'),
              testing::ElementsAre("aa", "bb", "ccc"));
  EXPECT_THAT(split_to<std::set<std::string>>("aa\nbb\nccc", '\n'),
              testing::ElementsAre("aa", "bb", "ccc"));
  EXPECT_THAT(split_to<std::vector<int>>("1,2,3", ','),
              testing::ElementsAre(1, 2, 3));
  EXPECT_THAT(split_to<std::set<int>>("3,4,3", ','),
              testing::ElementsAre(3, 4));
}
