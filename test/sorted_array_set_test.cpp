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
#include <concepts>
#include <string>
#include <string_view>
#include <utility>
#include <version>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <range/v3/range/conversion.hpp>
#include <range/v3/view/join.hpp>
#include <range/v3/view/reverse.hpp>

#include <dwarfs/container/sorted_array_set.h>

using namespace dwarfs::container;
using namespace std::string_view_literals;

namespace {

constexpr sorted_array_set set{1, 3, 2};

constexpr sorted_array_set sv_set{
    "one"sv, "two"sv, "three"sv, "four"sv, "five"sv,
};

constexpr sorted_array_set<std::string_view, 0> empty_set;

// a shuffled permutation of 0..N-1, to exercise the sort past the linear
// search threshold without spelling out every element
template <std::size_t N>
constexpr auto make_sort_test() {
  static_assert(N % 97 != 0);
  return []<std::size_t... I>(std::index_sequence<I...>) {
    return sorted_array_set{static_cast<int>((I * 97 + 13) % N)...};
  }(std::make_index_sequence<N>{});
}

constexpr auto sort_test = make_sort_test<250>();

static_assert(!set.empty());
static_assert(set.size() == 3);
static_assert(set.contains(1));
static_assert(set.contains(2));
static_assert(set.contains(3));
static_assert(!set.contains(0));
static_assert(!set.contains(4));
static_assert(set.count(1) == 1);
static_assert(set.count(4) == 0);
static_assert(set.find(2) != set.end());
static_assert(set.find(4) == set.end());
static_assert(*set.find(2) == 2);
static_assert(std::distance(set.begin(), set.end()) == 3);
static_assert(std::distance(set.cbegin(), set.cend()) == 3);
static_assert(std::distance(set.rbegin(), set.rend()) == 3);
static_assert(std::distance(set.crbegin(), set.crend()) == 3);
static_assert(*set.begin() == 1);
static_assert(*set.cbegin() == 1);
static_assert(*set.rbegin() == 3);
static_assert(*set.crbegin() == 3);

static_assert(!sv_set.empty());
static_assert(sv_set.size() == 5);
static_assert(sv_set.contains("one"sv));
static_assert(sv_set.contains("five"sv));
static_assert(!sv_set.contains("six"sv));
static_assert(*sv_set.begin() == "five"sv);
static_assert(*sv_set.rbegin() == "two"sv);

// should work with string literals
static_assert(sv_set.contains("one"));
static_assert(sv_set.contains("five"));
static_assert(!sv_set.contains("six"));
static_assert(sv_set.find("three") != sv_set.end());

#if defined(__cpp_lib_constexpr_string) && __cpp_lib_constexpr_string >= 201907L
// should also work with std::string
using namespace std::string_literals;
static_assert(sv_set.contains("one"s));
static_assert(sv_set.contains("five"s));
static_assert(!sv_set.contains("six"s));
#endif

static_assert(empty_set.empty());
static_assert(empty_set.size() == 0);
static_assert(empty_set.find("x"sv) == empty_set.end());
static_assert(!empty_set.contains("x"sv));
static_assert(empty_set.begin() == empty_set.end());
static_assert(std::distance(empty_set.begin(), empty_set.end()) == 0);

static_assert(sort_test.size() == 250);
static_assert(std::ranges::is_sorted(sort_test));
static_assert(*sort_test.begin() == 0);
static_assert(*sort_test.rbegin() == 249);
static_assert(sort_test.contains(0));
static_assert(sort_test.contains(137));
static_assert(sort_test.contains(249));
static_assert(!sort_test.contains(-1));
static_assert(!sort_test.contains(250));
static_assert(*sort_test.find(137) == 137);

constexpr sorted_array_set ctad_from_values{3, 1, 2};
static_assert(std::same_as<decltype(ctad_from_values) const,
                           sorted_array_set<int, 3> const>);

constexpr sorted_array_set ctad_from_array{std::array{3, 1, 2}};
static_assert(std::same_as<decltype(ctad_from_array) const,
                           sorted_array_set<int, 3> const>);

constexpr sorted_array_set ctad_from_empty_array{std::array<int, 0>{}};
static_assert(std::same_as<decltype(ctad_from_empty_array) const,
                           sorted_array_set<int, 0> const>);

constexpr sorted_array_set ctad_single{42};
static_assert(
    std::same_as<decltype(ctad_single) const, sorted_array_set<int, 1> const>);

// a lone array is always the element list, never a one-element set
static_assert(ctad_from_array.size() == 3);
static_assert(ctad_from_array.contains(2));

// ...but a set *of* arrays still deduces from two or more arguments
constexpr sorted_array_set ctad_array_elements{std::array{1, 2},
                                               std::array{3, 4}};
static_assert(std::same_as<decltype(ctad_array_elements) const,
                           sorted_array_set<std::array<int, 2>, 2> const>);
static_assert(ctad_array_elements.contains(std::array{3, 4}));

// copying a non-const lvalue must not be hijacked by the variadic constructor
static_assert([] {
  sorted_array_set s{1, 2, 3};
  auto copy = s;
  auto moved = std::move(copy);
  return moved.contains(2) && moved.size() == 3;
}());

// comparable with `int` but only explicitly constructible from one
struct code {
  int value;

  constexpr explicit code(int v)
      : value{v} {}

  constexpr bool operator==(code const&) const = default;
  constexpr auto operator<=>(code const&) const = default;

  constexpr bool operator==(int v) const { return value == v; }
  constexpr auto operator<=>(int v) const { return value <=> v; }
};

static_assert(!std::convertible_to<int, code>);
static_assert(detail::comparable_key<int, code>);

constexpr sorted_array_set code_set{code{3}, code{1}, code{2}};

// linear search path (N <= 32)
static_assert(code_set.contains(1));
static_assert(code_set.contains(3));
static_assert(!code_set.contains(4));
static_assert(code_set.count(2) == 1);
static_assert(code_set.find(2) != code_set.end());
static_assert(code_set.find(4) == code_set.end());
static_assert(code_set.contains(code{1}));
static_assert(!code_set.contains(code{4}));

// binary search path (N > 32), still heterogeneous
template <std::size_t N>
constexpr auto make_code_set() {
  return []<std::size_t... I>(std::index_sequence<I...>) {
    return sorted_array_set{code{static_cast<int>(sizeof...(I) - I)}...};
  }(std::make_index_sequence<N>{});
}

constexpr auto big_code_set = make_code_set<40>();

static_assert(big_code_set.size() == 40);
static_assert(big_code_set.contains(1));
static_assert(big_code_set.contains(40));
static_assert(!big_code_set.contains(0));
static_assert(!big_code_set.contains(41));
static_assert(big_code_set.find(41) == big_code_set.end());

// keys that make no sense are rejected by the constraints
template <typename Set, typename K>
concept has_lookup = requires(Set const& s, K const& k) { s.contains(k); };

static_assert(has_lookup<decltype(set), int>);
static_assert(!has_lookup<decltype(set), char const*>);
static_assert(has_lookup<decltype(sv_set), char const*>);
static_assert(has_lookup<decltype(code_set), int>);
static_assert(!has_lookup<decltype(code_set), char const*>);

template <typename S>
concept has_at = requires(S const& s) { s.at(1); };
template <typename S>
concept has_get = requires(S const& s) { s.get(1); };
template <typename S>
concept has_subscript = requires(S const& s) { s[1]; };

static_assert(!has_at<decltype(set)>);
static_assert(!has_get<decltype(set)>);
static_assert(!has_subscript<decltype(set)>);

static_assert(std::same_as<decltype(set)::key_type, int>);
static_assert(std::same_as<decltype(set)::value_type, int>);

} // namespace

TEST(sorted_array_set, constexpr_runtime) {
  EXPECT_EQ(set.size(), 3);
  EXPECT_TRUE(set.contains(1));
  EXPECT_TRUE(set.contains(2));
  EXPECT_TRUE(set.contains(3));
  EXPECT_FALSE(set.contains(4));
  EXPECT_EQ(set.count(1), 1);
  EXPECT_EQ(set.count(4), 0);
  EXPECT_NE(set.find(2), set.end());
  EXPECT_EQ(set.find(4), set.end());
  EXPECT_EQ(std::distance(set.begin(), set.end()), 3);
  EXPECT_EQ(std::distance(set.cbegin(), set.cend()), 3);
  EXPECT_EQ(std::distance(set.rbegin(), set.rend()), 3);
  EXPECT_EQ(std::distance(set.crbegin(), set.crend()), 3);

  EXPECT_THAT(set | ranges::to<std::vector>(), testing::ElementsAre(1, 2, 3));
  EXPECT_THAT(set | ranges::views::reverse | ranges::to<std::vector>(),
              testing::ElementsAre(3, 2, 1));

  EXPECT_EQ(sv_set.size(), 5);
  EXPECT_TRUE(sv_set.contains("one"sv));
  EXPECT_FALSE(sv_set.contains("six"sv));
  EXPECT_NE(sv_set.find("two"sv), sv_set.end());
  EXPECT_EQ(sv_set.find("six"sv), sv_set.end());

  EXPECT_EQ(sv_set | ranges::views::join(", "sv) | ranges::to<std::string>(),
            "five, four, one, three, two"sv);
  EXPECT_EQ(sv_set | ranges::views::reverse | ranges::views::join(", "sv) |
                ranges::to<std::string>(),
            "two, three, one, four, five"sv);

  EXPECT_EQ(sort_test.size(), 250);
  EXPECT_TRUE(sort_test.contains(137));
  EXPECT_FALSE(sort_test.contains(250));
}

TEST(sorted_array_set, const_runtime) {
  EXPECT_THAT(([] { sorted_array_set s{1, 2, 1}; }),
              testing::ThrowsMessage<std::invalid_argument>("Duplicate key"));

  sorted_array_set s{1, 3, 2};

  EXPECT_EQ(s.size(), 3);
  EXPECT_TRUE(s.contains(1));
  EXPECT_FALSE(s.contains(4));
  EXPECT_NE(s.find(2), s.end());
  EXPECT_EQ(s.find(4), s.end());
  EXPECT_EQ(std::distance(s.begin(), s.end()), 3);
  EXPECT_EQ(std::distance(s.crbegin(), s.crend()), 3);
  EXPECT_THAT(s | ranges::to<std::vector>(), testing::ElementsAre(1, 2, 3));

  auto copy = s;
  EXPECT_TRUE(copy.contains(2));
  auto moved = std::move(copy);
  EXPECT_TRUE(moved.contains(2));
}

TEST(sorted_array_set, heterogeneous_lookup_runtime) {
  sorted_array_set s{
      std::string{"one"},
      std::string{"three"},
      std::string{"two"},
  };

  static_assert(std::same_as<decltype(s)::key_type, std::string>);

  EXPECT_TRUE(s.contains("one"sv));
  EXPECT_TRUE(s.contains("three"sv));
  EXPECT_FALSE(s.contains("four"sv));
  EXPECT_NE(s.find("two"sv), s.end());
  EXPECT_EQ(s.find("four"sv), s.end());
  EXPECT_EQ(s.count("one"sv), 1);

  EXPECT_TRUE(s.contains("one"));
  EXPECT_TRUE(s.contains(std::string{"one"}));
}
