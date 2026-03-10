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

#include <string_view>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <dwarfs/conv.h>

using namespace std::string_view_literals;
using namespace dwarfs;

TEST(conversion, to_bool) {
  EXPECT_TRUE(to<bool>("1"sv));
  EXPECT_TRUE(to<bool>("true"sv));
  EXPECT_TRUE(to<bool>("TrUe"sv));
  EXPECT_TRUE(to<bool>("T"sv));
  EXPECT_TRUE(to<bool>("Y"sv));
  EXPECT_TRUE(to<bool>("Yes"sv));
  EXPECT_FALSE(to<bool>("0"sv));
  EXPECT_FALSE(to<bool>("false"sv));
  EXPECT_FALSE(to<bool>("FaLse"sv));
  EXPECT_FALSE(to<bool>("F"sv));
  EXPECT_FALSE(to<bool>("N"sv));
  EXPECT_FALSE(to<bool>("No"sv));
  EXPECT_THROW(to<bool>("foo"sv), std::bad_optional_access);
  EXPECT_THROW(to<bool>(""sv), std::bad_optional_access);
  EXPECT_THROW(to<bool>("2"sv), std::bad_optional_access);
  EXPECT_THROW(to<bool>("truee"sv), std::bad_optional_access);
  EXPECT_THROW(to<bool>("fals"sv), std::bad_optional_access);
}

TEST(conversion, to_integral) {
  EXPECT_EQ(to<int>("42"sv), 42);
  EXPECT_EQ(to<int>("-42"sv), -42);
  EXPECT_EQ(to<unsigned>("42"sv), 42U);
  EXPECT_THROW(to<unsigned>("-1"sv), std::bad_optional_access);
  EXPECT_THROW(to<int>("foo"sv), std::bad_optional_access);
  EXPECT_THROW(to<int>(""sv), std::bad_optional_access);
  EXPECT_THROW(to<int>("42.0"sv), std::bad_optional_access);
}
