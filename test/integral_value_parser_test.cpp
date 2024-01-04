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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "dwarfs/integral_value_parser.h"

using namespace dwarfs;

TEST(integral_value_parser_test, basic_check) {
  integral_value_parser<int16_t> p;

  EXPECT_EQ(42, p.parse("42"));
  EXPECT_EQ(-13, p.parse("-13"));
  EXPECT_THAT([&] { p.parse("42a"); },
              testing::Throws<folly::ConversionError>());
  EXPECT_THAT([&] { p.parse("40000"); },
              testing::Throws<folly::ConversionError>());
}

TEST(integral_value_parser_test, range_check) {
  integral_value_parser<int16_t> p{-20, 10};

  EXPECT_EQ(-20, p.parse("-20"));
  EXPECT_EQ(10, p.parse("10"));
  EXPECT_THAT([&] { p.parse("-21"); }, testing::Throws<std::range_error>());
  EXPECT_THAT([&] { p.parse("11"); }, testing::Throws<std::range_error>());
}

TEST(integral_value_parser_test, choice_check) {
  integral_value_parser<int16_t> p{1, 2, 3, 5, 8, 13};

  EXPECT_EQ(1, p.parse("1"));
  EXPECT_EQ(2, p.parse("2"));
  EXPECT_EQ(3, p.parse("3"));
  EXPECT_EQ(5, p.parse("5"));
  EXPECT_EQ(8, p.parse("8"));
  EXPECT_EQ(13, p.parse("13"));
  EXPECT_THAT([&] { p.parse("0"); }, testing::Throws<std::range_error>());
  EXPECT_THAT([&] { p.parse("4"); }, testing::Throws<std::range_error>());
  EXPECT_THAT([&] { p.parse("6"); }, testing::Throws<std::range_error>());
}

TEST(integral_value_parser_test, function_check) {
  integral_value_parser<int16_t> p{[](int16_t v) { return v % 2 == 0; }};

  EXPECT_EQ(0, p.parse("0"));
  EXPECT_EQ(2, p.parse("2"));
  EXPECT_EQ(4, p.parse("4"));
  EXPECT_EQ(6, p.parse("6"));
  EXPECT_EQ(-2, p.parse("-2"));
  EXPECT_THAT([&] { p.parse("1"); }, testing::Throws<std::range_error>());
  EXPECT_THAT([&] { p.parse("-3"); }, testing::Throws<std::range_error>());
}
