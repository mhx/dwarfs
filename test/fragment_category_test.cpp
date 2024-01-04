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

#include <algorithm>
#include <sstream>
#include <unordered_set>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <fmt/format.h>

#include "dwarfs/fragment_category.h"

using namespace dwarfs;

TEST(fragment_category_test, basic) {
  fragment_category c;

  EXPECT_TRUE(c.empty());
  EXPECT_FALSE(c);
  EXPECT_FALSE(c.has_subcategory());

  {
    std::ostringstream oss;
    oss << c;

    EXPECT_EQ("uninitialized", oss.str());
  }

  EXPECT_EQ("uninitialized", fmt::format("{}", c));

  c = fragment_category::value_type{42};

  EXPECT_FALSE(c.empty());
  EXPECT_TRUE(c);
  EXPECT_FALSE(c.has_subcategory());
  EXPECT_EQ(42, c.value());

  {
    std::ostringstream oss;
    oss << c;

    EXPECT_EQ("42", oss.str());
  }

  EXPECT_EQ("42", fmt::format("{}", c));

  c.set_subcategory(43);

  EXPECT_FALSE(c.empty());
  EXPECT_TRUE(c);
  EXPECT_TRUE(c.has_subcategory());
  EXPECT_EQ(42, c.value());
  EXPECT_EQ(43, c.subcategory());

  {
    std::ostringstream oss;
    oss << c;

    EXPECT_EQ("42.43", oss.str());
  }

  EXPECT_EQ("42.43", fmt::format("{}", c));

  c.clear();

  EXPECT_TRUE(c.empty());
  EXPECT_FALSE(c);
  EXPECT_FALSE(c.has_subcategory());
}

TEST(fragment_category_test, hash_table) {
  std::unordered_set<fragment_category> s;

  s.emplace(1);
  s.emplace(2, 3);
  s.emplace(4, 5);

  EXPECT_EQ(3, s.size());
  EXPECT_EQ(1, s.count(fragment_category{1}));
  EXPECT_EQ(1, s.count(fragment_category{2, 3}));
  EXPECT_EQ(1, s.count(fragment_category{4, 5}));
}

TEST(fragment_category_test, sortable) {
  std::vector<fragment_category> v;

  v.emplace_back(4, 5);
  v.emplace_back(1);
  v.emplace_back(2, 3);
  v.emplace_back(2);

  std::sort(v.begin(), v.end());

  EXPECT_EQ(fragment_category(1), v[0]);
  EXPECT_EQ(fragment_category(2, 3), v[1]);
  EXPECT_EQ(fragment_category(2), v[2]);
  EXPECT_EQ(fragment_category(4, 5), v[3]);
}
