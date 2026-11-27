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

#include <cmath>
#include <cstdint>
#include <limits>
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
  EXPECT_TRUE(to<bool>("y"sv));
  EXPECT_TRUE(to<bool>("yes"sv));
  EXPECT_FALSE(to<bool>("0"sv));
  EXPECT_FALSE(to<bool>("false"sv));
  EXPECT_FALSE(to<bool>("FaLse"sv));
  EXPECT_FALSE(to<bool>("F"sv));
  EXPECT_FALSE(to<bool>("N"sv));
  EXPECT_FALSE(to<bool>("No"sv));
  EXPECT_FALSE(to<bool>("n"sv));
  EXPECT_FALSE(to<bool>("no"sv));
  EXPECT_THROW(to<bool>("foo"sv), dwarfs::conversion_error);
  EXPECT_THROW(to<bool>(""sv), dwarfs::conversion_error);
  EXPECT_THROW(to<bool>("2"sv), dwarfs::conversion_error);
  EXPECT_THROW(to<bool>("truee"sv), dwarfs::conversion_error);
  EXPECT_THROW(to<bool>("fals"sv), dwarfs::conversion_error);
}

TEST(conversion, to_integral) {
  EXPECT_EQ(to<int>("42"sv), 42);
  EXPECT_EQ(to<int>("-42"sv), -42);
  EXPECT_EQ(to<unsigned>("42"sv), 42U);
  EXPECT_THROW(to<unsigned>("-1"sv), dwarfs::conversion_error);
  EXPECT_THROW(to<int>("foo"sv), dwarfs::conversion_error);
  EXPECT_THROW(to<int>(""sv), dwarfs::conversion_error);
  EXPECT_THROW(to<int>("42.0"sv), dwarfs::conversion_error);
}

TEST(conversion, integer_boundaries) {
  EXPECT_EQ(to<int>("+42"), 42);
  EXPECT_EQ(to<int>(std::string("-0")), 0);
  EXPECT_EQ(to<int16_t>("-32768"), std::numeric_limits<int16_t>::min());
  EXPECT_EQ(to<int16_t>("32767"), std::numeric_limits<int16_t>::max());
  EXPECT_EQ(to<int64_t>("-9223372036854775808"),
            std::numeric_limits<int64_t>::min());
  EXPECT_EQ(to<uint64_t>("18446744073709551615"),
            std::numeric_limits<uint64_t>::max());
  EXPECT_FALSE(try_to<int16_t>("32768"));
  EXPECT_FALSE(try_to<int16_t>("-32769"));
  EXPECT_FALSE(try_to<int64_t>("9223372036854775808"));
  EXPECT_FALSE(try_to<uint64_t>("18446744073709551616"));
  EXPECT_FALSE(try_to<unsigned>("+42"));
  EXPECT_FALSE(try_to<unsigned>("-0"));
  for (auto s : {" 42"sv, "42 "sv, "+"sv, "-"sv, "+-42"sv, "++42"sv, "0x2a"sv,
                 "42\0x"sv}) {
    EXPECT_FALSE(try_to<int>(s)) << s;
  }
  EXPECT_EQ(to<int>("0042"), 42);
}

TEST(conversion, to_floating_point) {
  EXPECT_EQ(to<double>("+1.25"), 1.25);
  EXPECT_EQ(to<double>(".5"), 0.5);
  EXPECT_EQ(to<double>("1."), 1.0);
  EXPECT_EQ(to<double>("-2.5e+2"), -250.0);
  EXPECT_EQ(to<float>("0.125"), 0.125f);
  EXPECT_TRUE(std::signbit(to<double>("-0")));
  EXPECT_TRUE(std::isinf(to<double>("+inf")));
  EXPECT_TRUE(std::isinf(to<double>("-Infinity")));
  EXPECT_TRUE(std::isnan(to<double>("NaN")));
  for (auto s : {""sv, " 1"sv, "1 "sv, "1e"sv, "+-1"sv, "0x1p0"sv, "1,5"sv,
                 "1e999"sv, "1e-999"sv, "1\0x"sv}) {
    EXPECT_FALSE(try_to<double>(s)) << s;
  }
}

TEST(conversion, to_string) {
  EXPECT_EQ(to<std::string>(int16_t{-42}), "-42");
  EXPECT_EQ(to<std::string>(std::numeric_limits<int64_t>::min()),
            "-9223372036854775808");
  EXPECT_EQ(to<std::string>(std::numeric_limits<uint64_t>::max()),
            "18446744073709551615");
  EXPECT_EQ(to<std::string>("42"sv), "42");
  EXPECT_EQ(to<std::string>(""sv), "");
  EXPECT_EQ(to<std::string>("a\0b"sv), std::string("a\0b", 3));
  EXPECT_EQ(to<std::string>(1.25), "1.25");
}
