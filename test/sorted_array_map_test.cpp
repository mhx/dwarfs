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
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <string>
#include <string_view>
#include <version>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <range/v3/range/conversion.hpp>
#include <range/v3/view/join.hpp>
#include <range/v3/view/map.hpp>
#include <range/v3/view/reverse.hpp>

#include <dwarfs/sorted_array_map.h>

using namespace dwarfs;
using namespace std::string_view_literals;

namespace {

constexpr sorted_array_map map{
    std::pair{1, "one"sv},
    std::pair{3, "three"sv},
    std::pair{2, "two"sv},
};

constexpr sorted_array_map sv_map{
    std::pair{"one"sv, 1},  std::pair{"two"sv, 2},  std::pair{"three"sv, 3},
    std::pair{"four"sv, 4}, std::pair{"five"sv, 5},
};

constexpr sorted_array_map<int, std::string_view, 0> empty_map;

constexpr sorted_array_map sort_test{
    std::pair{53, 53},   std::pair{29, 29},   std::pair{59, 59},
    std::pair{196, 196}, std::pair{7, 7},     std::pair{242, 242},
    std::pair{139, 139}, std::pair{237, 237}, std::pair{165, 165},
    std::pair{92, 92},   std::pair{204, 204}, std::pair{209, 209},
    std::pair{183, 183}, std::pair{110, 110}, std::pair{76, 76},
    std::pair{152, 152}, std::pair{164, 164}, std::pair{248, 248},
    std::pair{233, 233}, std::pair{130, 130}, std::pair{180, 180},
    std::pair{236, 236}, std::pair{230, 230}, std::pair{208, 208},
    std::pair{192, 192}, std::pair{238, 238}, std::pair{40, 40},
    std::pair{163, 163}, std::pair{6, 6},     std::pair{44, 44},
    std::pair{17, 17},   std::pair{140, 140}, std::pair{16, 16},
    std::pair{19, 19},   std::pair{149, 149}, std::pair{67, 67},
    std::pair{66, 66},   std::pair{127, 127}, std::pair{199, 199},
    std::pair{234, 234}, std::pair{135, 135}, std::pair{46, 46},
    std::pair{108, 108}, std::pair{32, 32},   std::pair{212, 212},
    std::pair{194, 194}, std::pair{58, 58},   std::pair{12, 12},
    std::pair{106, 106}, std::pair{240, 240}, std::pair{97, 97},
    std::pair{154, 154}, std::pair{98, 98},   std::pair{215, 215},
    std::pair{79, 79},   std::pair{223, 223}, std::pair{80, 80},
    std::pair{173, 173}, std::pair{55, 55},   std::pair{27, 27},
    std::pair{52, 52},   std::pair{100, 100}, std::pair{126, 126},
    std::pair{11, 11},   std::pair{198, 198}, std::pair{47, 47},
    std::pair{147, 147}, std::pair{91, 91},   std::pair{132, 132},
    std::pair{121, 121}, std::pair{160, 160}, std::pair{239, 239},
    std::pair{75, 75},   std::pair{202, 202}, std::pair{177, 177},
    std::pair{51, 51},   std::pair{241, 241}, std::pair{244, 244},
    std::pair{250, 250}, std::pair{23, 23},   std::pair{171, 171},
    std::pair{42, 42},   std::pair{172, 172}, std::pair{136, 136},
    std::pair{43, 43},   std::pair{48, 48},   std::pair{13, 13},
    std::pair{169, 169}, std::pair{245, 245}, std::pair{54, 54},
    std::pair{101, 101}, std::pair{89, 89},   std::pair{142, 142},
    std::pair{83, 83},   std::pair{34, 34},   std::pair{56, 56},
    std::pair{31, 31},   std::pair{235, 235}, std::pair{137, 137},
    std::pair{228, 228}, std::pair{73, 73},   std::pair{21, 21},
    std::pair{71, 71},   std::pair{232, 232}, std::pair{210, 210},
    std::pair{70, 70},   std::pair{14, 14},   std::pair{119, 119},
    std::pair{227, 227}, std::pair{213, 213}, std::pair{123, 123},
    std::pair{203, 203}, std::pair{81, 81},   std::pair{197, 197},
    std::pair{113, 113}, std::pair{87, 87},   std::pair{22, 22},
    std::pair{218, 218}, std::pair{125, 125}, std::pair{214, 214},
    std::pair{151, 151}, std::pair{96, 96},   std::pair{86, 86},
    std::pair{124, 124}, std::pair{189, 189}, std::pair{120, 120},
    std::pair{220, 220}, std::pair{129, 129}, std::pair{191, 191},
    std::pair{82, 82},   std::pair{145, 145}, std::pair{138, 138},
    std::pair{26, 26},   std::pair{62, 62},   std::pair{117, 117},
    std::pair{60, 60},   std::pair{168, 168}, std::pair{4, 4},
    std::pair{104, 104}, std::pair{36, 36},   std::pair{50, 50},
    std::pair{78, 78},   std::pair{131, 131}, std::pair{157, 157},
    std::pair{229, 229}, std::pair{148, 148}, std::pair{77, 77},
    std::pair{144, 144}, std::pair{88, 88},   std::pair{118, 118},
    std::pair{133, 133}, std::pair{39, 39},   std::pair{150, 150},
    std::pair{37, 37},   std::pair{159, 159}, std::pair{122, 122},
    std::pair{193, 193}, std::pair{222, 222}, std::pair{247, 247},
    std::pair{128, 128}, std::pair{184, 184}, std::pair{185, 185},
    std::pair{166, 166}, std::pair{85, 85},   std::pair{190, 190},
    std::pair{195, 195}, std::pair{156, 156}, std::pair{170, 170},
    std::pair{205, 205}, std::pair{105, 105}, std::pair{200, 200},
    std::pair{226, 226}, std::pair{94, 94},   std::pair{3, 3},
    std::pair{72, 72},   std::pair{109, 109}, std::pair{30, 30},
    std::pair{217, 217}, std::pair{115, 115}, std::pair{33, 33},
    std::pair{225, 225}, std::pair{15, 15},   std::pair{68, 68},
    std::pair{99, 99},   std::pair{103, 103}, std::pair{64, 64},
    std::pair{188, 188}, std::pair{45, 45},   std::pair{206, 206},
    std::pair{179, 179}, std::pair{93, 93},   std::pair{69, 69},
    std::pair{178, 178}, std::pair{24, 24},   std::pair{2, 2},
    std::pair{162, 162}, std::pair{61, 61},   std::pair{181, 181},
    std::pair{219, 219}, std::pair{84, 84},   std::pair{243, 243},
    std::pair{107, 107}, std::pair{231, 231}, std::pair{201, 201},
    std::pair{112, 112}, std::pair{102, 102}, std::pair{49, 49},
    std::pair{161, 161}, std::pair{155, 155}, std::pair{114, 114},
    std::pair{95, 95},   std::pair{146, 146}, std::pair{8, 8},
    std::pair{158, 158}, std::pair{174, 174}, std::pair{90, 90},
    std::pair{1, 1},     std::pair{143, 143}, std::pair{211, 211},
    std::pair{246, 246}, std::pair{25, 25},   std::pair{41, 41},
    std::pair{111, 111}, std::pair{153, 153}, std::pair{167, 167},
    std::pair{224, 224}, std::pair{20, 20},   std::pair{141, 141},
    std::pair{175, 175}, std::pair{10, 10},   std::pair{63, 63},
    std::pair{9, 9},     std::pair{134, 134}, std::pair{38, 38},
    std::pair{116, 116}, std::pair{18, 18},   std::pair{182, 182},
    std::pair{57, 57},   std::pair{186, 186}, std::pair{221, 221},
    std::pair{216, 216}, std::pair{207, 207}, std::pair{65, 65},
    std::pair{187, 187}, std::pair{28, 28},   std::pair{35, 35},
    std::pair{5, 5},     std::pair{176, 176}, std::pair{74, 74},
    std::pair{249, 249},
};

static_assert(!map.empty());
static_assert(map.size() == 3);
static_assert(map.at(1) == "one"sv);
static_assert(map[2] == "two"sv);
static_assert(map.at(3) == "three"sv);
static_assert(map.get(1).has_value());
static_assert(!map.get(0).has_value());
static_assert(map.contains(1));
static_assert(!map.contains(4));
static_assert(map.find(2) != map.end());
static_assert(map.find(4) == map.end());
static_assert(std::distance(map.begin(), map.end()) == 3);
static_assert(std::distance(map.cbegin(), map.cend()) == 3);
static_assert(std::distance(map.rbegin(), map.rend()) == 3);
static_assert(std::distance(map.crbegin(), map.crend()) == 3);
static_assert(map.begin()->first == 1);
static_assert(map.cbegin()->first == 1);
static_assert(map.rbegin()->first == 3);
static_assert(map.crbegin()->first == 3);

static_assert(!sv_map.empty());
static_assert(sv_map.size() == 5);
static_assert(sv_map.at("one"sv) == 1);
static_assert(sv_map["two"sv] == 2);
static_assert(sv_map.at("three"sv) == 3);
static_assert(sv_map.get("four"sv).has_value());
static_assert(!sv_map.get("zero"sv).has_value());
static_assert(sv_map.contains("five"sv));
static_assert(!sv_map.contains("six"sv));

// should work with string literals
static_assert(sv_map.at("one") == 1);
static_assert(sv_map["two"] == 2);
static_assert(sv_map.at("three") == 3);
static_assert(sv_map.get("four").has_value());
static_assert(!sv_map.get("zero").has_value());
static_assert(sv_map.contains("five"));
static_assert(!sv_map.contains("six"));

#if defined(__cpp_lib_constexpr_string) && __cpp_lib_constexpr_string >= 201907L
// should also work with std::string
using namespace std::string_literals;
static_assert(sv_map.at("one"s) == 1);
static_assert(sv_map["two"s] == 2);
static_assert(sv_map.at("three"s) == 3);
static_assert(sv_map.get("four"s).has_value());
static_assert(!sv_map.get("zero"s).has_value());
static_assert(sv_map.contains("five"s));
static_assert(!sv_map.contains("six"s));
#endif

static_assert(empty_map.empty());
static_assert(empty_map.size() == 0);
static_assert(empty_map.find(0) == empty_map.end());
static_assert(empty_map.begin() == empty_map.end());
static_assert(std::distance(empty_map.begin(), empty_map.end()) == 0);

static_assert(sort_test.size() == 250);
static_assert(std::is_sorted(sort_test.begin(), sort_test.end()));

} // namespace

TEST(sorted_array_map, constexpr_runtime) {
  EXPECT_THAT([] { map.at(0); }, testing::Throws<std::out_of_range>());
  EXPECT_THAT([] { map[4]; }, testing::Throws<std::out_of_range>());

  EXPECT_EQ(map.size(), 3);
  EXPECT_EQ(map.at(1), "one"sv);
  EXPECT_EQ(map[2], "two"sv);
  EXPECT_EQ(map.at(3), "three"sv);
  EXPECT_EQ(map.get(1).value(), "one"sv);
  EXPECT_FALSE(map.get(0).has_value());
  EXPECT_TRUE(map.contains(1));
  EXPECT_FALSE(map.contains(4));
  EXPECT_NE(map.find(2), map.end());
  EXPECT_EQ(map.find(4), map.end());
  EXPECT_EQ(std::distance(map.begin(), map.end()), 3);
  EXPECT_EQ(std::distance(map.cbegin(), map.cend()), 3);
  EXPECT_EQ(std::distance(map.rbegin(), map.rend()), 3);
  EXPECT_EQ(std::distance(map.crbegin(), map.crend()), 3);

  EXPECT_THAT(map | ranges::views::keys | ranges::to<std::vector>(),
              testing::ElementsAre(1, 2, 3));
  EXPECT_EQ(map | ranges::views::reverse | ranges::views::values |
                ranges::views::join(", "sv) | ranges::to<std::string>(),
            "three, two, one"sv);

  EXPECT_THAT([] { sv_map.at("zero"sv); },
              testing::Throws<std::out_of_range>());
  EXPECT_THAT([] { sv_map["six"sv]; }, testing::Throws<std::out_of_range>());

  EXPECT_EQ(sv_map.size(), 5);
  EXPECT_EQ(sv_map.at("one"sv), 1);
  EXPECT_EQ(sv_map["two"sv], 2);
  EXPECT_EQ(sv_map.at("three"sv), 3);
  EXPECT_EQ(sv_map.get("four"sv).value(), 4);
  EXPECT_FALSE(sv_map.get("zero"sv).has_value());
  EXPECT_TRUE(sv_map.contains("five"sv));
  EXPECT_FALSE(sv_map.contains("six"sv));
  EXPECT_NE(sv_map.find("two"sv), sv_map.end());
  EXPECT_EQ(sv_map.find("six"sv), sv_map.end());
  EXPECT_EQ(std::distance(sv_map.begin(), sv_map.end()), 5);
  EXPECT_EQ(std::distance(sv_map.cbegin(), sv_map.cend()), 5);
  EXPECT_EQ(std::distance(sv_map.rbegin(), sv_map.rend()), 5);
  EXPECT_EQ(std::distance(sv_map.crbegin(), sv_map.crend()), 5);

  EXPECT_EQ(sv_map | ranges::views::keys | ranges::views::join(", "sv) |
                ranges::to<std::string>(),
            "five, four, one, three, two"sv);
  EXPECT_THAT(sv_map | ranges::views::values | ranges::views::reverse |
                  ranges::to<std::vector>(),
              testing::ElementsAre(2, 3, 1, 4, 5));
}

TEST(sorted_array_map, const_runtime) {
  EXPECT_THAT(([] {
                sorted_array_map m{
                    std::pair{1, "one"sv},
                    std::pair{1, "anotherone"sv},
                };
              }),
              testing::ThrowsMessage<std::invalid_argument>("Duplicate key"));

  sorted_array_map m{
      std::pair{1, "one"sv},
      std::pair{3, "three"sv},
      std::pair{2, "two"sv},
  };

  EXPECT_EQ(m.size(), 3);
  EXPECT_EQ(m.at(1), "one"sv);
  EXPECT_EQ(m[2], "two"sv);
  EXPECT_EQ(m.at(3), "three"sv);
  EXPECT_EQ(m.get(1).value(), "one"sv);
  EXPECT_FALSE(m.get(0).has_value());
  EXPECT_TRUE(m.contains(1));
  EXPECT_FALSE(m.contains(4));
  EXPECT_NE(m.find(2), m.end());
  EXPECT_EQ(m.find(4), m.end());
  EXPECT_EQ(std::distance(m.begin(), m.end()), 3);
  EXPECT_EQ(std::distance(m.cbegin(), m.cend()), 3);
  EXPECT_EQ(std::distance(m.rbegin(), m.rend()), 3);
  EXPECT_EQ(std::distance(m.crbegin(), m.crend()), 3);
}
