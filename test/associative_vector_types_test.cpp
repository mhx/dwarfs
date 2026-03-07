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
#include <type_traits>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <dwarfs/internal/associative_vector_types.h>

namespace {

using namespace dwarfs::internal;

using testing::ElementsAre;
using testing::Pair;

struct multi_arg_value {
  int number;
  std::string text;

  multi_arg_value(int n, std::string t)
      : number(n)
      , text(std::move(t)) {}

  auto operator==(multi_arg_value const&) const -> bool = default;
};

static_assert(std::is_same_v<
              decltype(std::declval<small_vector_set<int> const&>().begin()),
              small_vector_set<int>::const_iterator>);

static_assert(std::is_const_v<
              typename small_vector_map<int, int>::value_type::first_type>);

} // anonymous namespace

TEST(small_vector_set_test, default_constructed_is_empty) {
  small_vector_set<int> set;

  EXPECT_TRUE(set.empty());
  EXPECT_EQ(set.size(), 0u);
  EXPECT_THAT(set, ElementsAre());
  EXPECT_FALSE(set.contains(123));
}

TEST(small_vector_set_test, insert_adds_unique_elements_and_preserves_order) {
  small_vector_set<int> set;

  auto [it1, inserted1] = set.insert(3);
  EXPECT_TRUE(inserted1);
  EXPECT_EQ(*it1, 3);

  auto [it2, inserted2] = set.insert(1);
  EXPECT_TRUE(inserted2);
  EXPECT_EQ(*it2, 1);

  auto [it3, inserted3] = set.insert(2);
  EXPECT_TRUE(inserted3);
  EXPECT_EQ(*it3, 2);

  EXPECT_FALSE(set.empty());
  EXPECT_EQ(set.size(), 3u);
  EXPECT_TRUE(set.contains(3));
  EXPECT_TRUE(set.contains(1));
  EXPECT_TRUE(set.contains(2));
  EXPECT_FALSE(set.contains(4));

  EXPECT_THAT(set, ElementsAre(3, 1, 2));
}

TEST(small_vector_set_test, insert_existing_element_returns_existing_iterator) {
  small_vector_set<int> set;

  auto [first_it, first_inserted] = set.insert(42);
  auto [second_it, second_inserted] = set.insert(42);

  EXPECT_TRUE(first_inserted);
  EXPECT_FALSE(second_inserted);

  EXPECT_EQ(*first_it, 42);
  EXPECT_EQ(*second_it, 42);
  EXPECT_EQ(std::addressof(*first_it), std::addressof(*second_it));

  EXPECT_EQ(set.size(), 1u);
  EXPECT_THAT(set, ElementsAre(42));
}

TEST(small_vector_set_test, contains_tracks_inserted_values) {
  small_vector_set<std::string> set;

  EXPECT_FALSE(set.contains("alpha"));
  EXPECT_FALSE(set.contains("beta"));

  set.insert("alpha");

  EXPECT_TRUE(set.contains("alpha"));
  EXPECT_FALSE(set.contains("beta"));
}

TEST(small_vector_map_test, default_constructed_is_empty) {
  small_vector_map<int, std::string> map;

  EXPECT_TRUE(map.empty());
  EXPECT_EQ(map.size(), 0u);
  EXPECT_EQ(map.find(123), map.end());
  EXPECT_THAT(map, ElementsAre());
}

TEST(small_vector_map_test, emplace_adds_unique_keys_and_preserves_order) {
  small_vector_map<int, std::string> map;

  auto [it1, inserted1] = map.emplace(3, "three");
  EXPECT_TRUE(inserted1);
  EXPECT_EQ(it1->first, 3);
  EXPECT_EQ(it1->second, "three");

  auto [it2, inserted2] = map.emplace(1, "one");
  EXPECT_TRUE(inserted2);
  EXPECT_EQ(it2->first, 1);
  EXPECT_EQ(it2->second, "one");

  auto [it3, inserted3] = map.emplace(2, "two");
  EXPECT_TRUE(inserted3);
  EXPECT_EQ(it3->first, 2);
  EXPECT_EQ(it3->second, "two");

  EXPECT_FALSE(map.empty());
  EXPECT_EQ(map.size(), 3u);

  EXPECT_NE(map.find(3), map.end());
  EXPECT_NE(map.find(1), map.end());
  EXPECT_NE(map.find(2), map.end());
  EXPECT_EQ(map.find(4), map.end());

  EXPECT_THAT(map,
              ElementsAre(Pair(3, "three"), Pair(1, "one"), Pair(2, "two")));
}

TEST(small_vector_map_test,
     emplace_existing_key_returns_existing_iterator_and_keeps_original_value) {
  small_vector_map<int, std::string> map;

  auto [first_it, first_inserted] = map.emplace(7, "first");
  auto [second_it, second_inserted] = map.emplace(7, "second");

  EXPECT_TRUE(first_inserted);
  EXPECT_FALSE(second_inserted);

  EXPECT_EQ(first_it->first, 7);
  EXPECT_EQ(first_it->second, "first");
  EXPECT_EQ(second_it->first, 7);
  EXPECT_EQ(second_it->second, "first");
  EXPECT_EQ(std::addressof(*first_it), std::addressof(*second_it));

  EXPECT_EQ(map.size(), 1u);
  EXPECT_THAT(map, ElementsAre(Pair(7, "first")));
}

TEST(small_vector_map_test,
     emplace_piecewise_constructs_mapped_type_from_multiple_arguments) {
  small_vector_map<int, multi_arg_value> map;

  auto [it, inserted] = map.emplace(5, 123, "hello");

  EXPECT_TRUE(inserted);
  EXPECT_EQ(map.size(), 1u);
  EXPECT_EQ(it->first, 5);
  EXPECT_EQ(it->second.number, 123);
  EXPECT_EQ(it->second.text, "hello");

  auto found = map.find(5);
  ASSERT_NE(found, map.end());
  EXPECT_EQ(found->second, (multi_arg_value{123, "hello"}));
}

TEST(small_vector_map_test, find_returns_iterator_to_existing_element) {
  small_vector_map<std::string, int> map;
  map.emplace("alpha", 1);
  map.emplace("beta", 2);

  auto it = map.find("beta");

  ASSERT_NE(it, map.end());
  EXPECT_EQ(it->first, "beta");
  EXPECT_EQ(it->second, 2);
}

TEST(small_vector_map_test, find_returns_end_for_missing_key) {
  small_vector_map<std::string, int> map;
  map.emplace("alpha", 1);

  EXPECT_EQ(map.find("missing"), map.end());
}
