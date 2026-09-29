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

#include <array>
#include <limits>
#include <optional>
#include <ostream>
#include <string_view>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <dwarfs/semver.h>

using namespace dwarfs;
using namespace std::string_view_literals;

using ::testing::Eq;
using ::testing::FieldsAre;
using ::testing::Optional;

namespace {

struct valid_case {
  std::string_view input;
  unsigned major, minor, patch;

  friend std::ostream& operator<<(std::ostream& os, valid_case const& c) {
    return os << '"' << c.input << '"';
  }
};

class semver_parse_valid : public ::testing::TestWithParam<valid_case> {};

constexpr std::array kValidTestCases{
    valid_case{"0.0.0", 0, 0, 0},
    valid_case{"1.2.3", 1, 2, 3},
    valid_case{"10.20.30", 10, 20, 30},
    valid_case{"0.15.3-683-gddcad4fe50-dirty", 0, 15, 3},
    valid_case{"0.15.3-dirty", 0, 15, 3},
    valid_case{"1.2.3-rc.1", 1, 2, 3},
    valid_case{"1.2.3+build.42", 1, 2, 3},
    valid_case{"1.2.3-rc.1+build.42", 1, 2, 3},
    valid_case{"1.2.3-", 1, 2, 3},
    valid_case{"4294967295.0.0", std::numeric_limits<unsigned>::max(), 0, 0},
};

class semver_parse_invalid : public ::testing::TestWithParam<std::string_view> {
};

constexpr std::array kInvalidTestCases{
    ""sv,        "1"sv,      "1.2"sv,    "1.2."sv,   "1..3"sv,   ".1.2.3"sv,
    "1.2.3.4"sv, "1.2.3x"sv, "1.2.3 "sv, " 1.2.3"sv, "1 .2.3"sv, "a.b.c"sv,
    "1.b.3"sv,   "v1.2.3"sv, "-1.2.3"sv, "+1.2.3"sv, "1.-2.3"sv,
};

} // namespace

TEST_P(semver_parse_valid, parses_components) {
  auto const& c = GetParam();
  EXPECT_THAT(semver::parse(c.input),
              Optional(FieldsAre(c.major, c.minor, c.patch)));
}

INSTANTIATE_TEST_SUITE_P(semver, semver_parse_valid,
                         ::testing::ValuesIn(kValidTestCases));

TEST_P(semver_parse_invalid, returns_nullopt) {
  EXPECT_THAT(semver::parse(GetParam()), Eq(std::nullopt));
}

INSTANTIATE_TEST_SUITE_P(semver, semver_parse_invalid,
                         ::testing::ValuesIn(kInvalidTestCases));

TEST(semver, strips_matching_prefix) {
  EXPECT_THAT(semver::parse("v1.2.3", "v"), Optional(FieldsAre(1, 2, 3)));
  EXPECT_THAT(semver::parse("release-1.2.3", "release-"),
              Optional(FieldsAre(1, 2, 3)));
}

TEST(semver, prefix_is_optional) {
  EXPECT_THAT(semver::parse("1.2.3", "v"), Optional(FieldsAre(1, 2, 3)));
}

TEST(semver, empty_prefix_is_ignored) {
  EXPECT_THAT(semver::parse("1.2.3", ""), Optional(FieldsAre(1, 2, 3)));
  EXPECT_THAT(semver::parse("v1.2.3", ""), Eq(std::nullopt));
}

TEST(semver, different_prefix_is_rejected) {
  EXPECT_THAT(semver::parse("x1.2.3", "v"), Eq(std::nullopt));
}

TEST(semver, prefix_is_case_sensitive) {
  EXPECT_THAT(semver::parse("V1.2.3", "v"), Eq(std::nullopt));
}

TEST(semver, prefix_is_stripped_only_once) {
  EXPECT_THAT(semver::parse("vv1.2.3", "v"), Eq(std::nullopt));
}

TEST(semver, prefix_must_be_followed_by_version) {
  EXPECT_THAT(semver::parse("v", "v"), Eq(std::nullopt));
}

TEST(semver, patch_defaults_to_zero) {
  EXPECT_THAT(semver(1, 2), FieldsAre(1, 2, 0));
  EXPECT_EQ(semver(1, 2), semver(1, 2, 0));
}

TEST(semver, equality) {
  EXPECT_EQ(semver(1, 2, 3), semver(1, 2, 3));
  EXPECT_NE(semver(1, 2, 3), semver(1, 2, 4));
  EXPECT_NE(semver(1, 2, 3), semver(1, 3, 3));
  EXPECT_NE(semver(1, 2, 3), semver(2, 2, 3));
}

TEST(semver, orders_by_patch) {
  EXPECT_LT(semver(1, 2, 3), semver(1, 2, 4));
  EXPECT_GT(semver(1, 2, 4), semver(1, 2, 3));
}

TEST(semver, orders_by_minor) { EXPECT_LT(semver(1, 2, 9), semver(1, 3, 0)); }

TEST(semver, orders_by_major) { EXPECT_LT(semver(0, 99, 99), semver(1, 0, 0)); }

TEST(semver, compares_numerically) {
  EXPECT_LT(semver(1, 9, 0), semver(1, 10, 0));
  EXPECT_LT(*semver::parse("1.9.0"), *semver::parse("1.10.0"));
}

TEST(semver, three_way_comparison) {
  EXPECT_EQ(semver(1, 2, 3) <=> semver(1, 2, 3), std::strong_ordering::equal);
  EXPECT_EQ(semver(1, 2, 3) <=> semver(1, 2, 4), std::strong_ordering::less);
  EXPECT_EQ(semver(2, 0, 0) <=> semver(1, 9, 9), std::strong_ordering::greater);
}

TEST(semver, prefix_works_with_suffix) {
  EXPECT_THAT(semver::parse("v0.15.3-683-gddcad4fe50-dirty", "v"),
              Optional(FieldsAre(0, 15, 3)));
}

TEST(semver, suffix_is_ignored) {
  EXPECT_EQ(*semver::parse("1.2.3-dirty"), semver(1, 2, 3));
  EXPECT_EQ(*semver::parse("1.2.3-rc.1"), *semver::parse("1.2.3+build.7"));
}

TEST(semver, libdwarfs_with_git_describe) {
  std::string_view v = "libwarfs 0.15.3-683-gddcad4fe50-dirty";
  auto ver = semver::parse(v, "libwarfs ");
  ASSERT_TRUE(ver.has_value());
  EXPECT_TRUE(*ver < semver(0, 16));
  EXPECT_TRUE(*ver >= semver(0, 15, 3));
}

TEST(semver, stringify_to_ostream) {
  std::ostringstream oss;
  oss << semver(1, 2, 3);
  EXPECT_EQ(oss.str(), "1.2.3");
}
