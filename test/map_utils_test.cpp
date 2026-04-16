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

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <parallel_hashmap/phmap.h>

#include <dwarfs/container/map_utils.h>
#include <dwarfs/detail/string_like_hash.h>

namespace {

using namespace dwarfs::container;
using namespace std::string_view_literals;
using dwarfs::detail::string_like_hash;
using ::testing::Eq;
using ::testing::Optional;

struct string_map {
  template <typename T>
  using map_type = std::map<std::string, T, std::less<>>;
};

struct unordered_string_map {
  template <typename T>
  using map_type =
      std::unordered_map<std::string, T, string_like_hash, std::equal_to<>>;
};

struct phmap_string_map {
  template <typename T>
  using map_type =
      phmap::flat_hash_map<std::string, T, string_like_hash, std::equal_to<>>;
};

template <typename Map>
auto make_string_int_map() -> Map {
  return Map{
      {"alpha", 1},
      {"beta", 2},
      {"gamma", 3},
  };
}

} // namespace

template <typename Map>
class string_keyed_map_test : public ::testing::Test {};

using string_keyed_map_types =
    ::testing::Types<string_map, unordered_string_map, phmap_string_map>;

TYPED_TEST_SUITE(string_keyed_map_test, string_keyed_map_types);

TYPED_TEST(string_keyed_map_test,
           get_optional_finds_existing_value_with_heterogeneous_lookup) {
  using map_type = typename TypeParam::template map_type<int>;
  auto map = make_string_int_map<map_type>();

  EXPECT_THAT(get_optional(map, "beta"sv), Optional(Eq(2)));
}

TYPED_TEST(string_keyed_map_test,
           get_optional_returns_nullopt_for_missing_key) {
  using map_type = typename TypeParam::template map_type<int>;
  auto map = make_string_int_map<map_type>();

  EXPECT_THAT(get_optional(map, "delta"sv), Eq(std::nullopt));
}

TYPED_TEST(string_keyed_map_test,
           get_optional_can_be_used_with_monadic_operations) {
  using map_type = typename TypeParam::template map_type<int>;
  auto map = make_string_int_map<map_type>();

  auto result = get_optional(map, "gamma"sv).transform([](int value) {
    return value * 10;
  });

  EXPECT_THAT(result, Optional(Eq(30)));
}

TYPED_TEST(string_keyed_map_test, get_optional_ref_returns_mutable_reference) {
  using map_type = typename TypeParam::template map_type<int>;
  auto map = make_string_int_map<map_type>();

  auto value = get_optional_ref(map, "alpha"sv);

  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(&value->get(), &map.find("alpha")->second);

  value->get() = 11;

  EXPECT_THAT(get_optional(map, "alpha"sv), Optional(Eq(11)));
}

TYPED_TEST(string_keyed_map_test,
           get_optional_ref_returns_nullopt_for_missing_key) {
  using map_type = typename TypeParam::template map_type<int>;
  auto map = make_string_int_map<map_type>();

  EXPECT_THAT(get_optional_ref(map, "missing"sv), Eq(std::nullopt));
}

TYPED_TEST(string_keyed_map_test,
           get_optional_ref_on_const_container_returns_const_reference) {
  using map_type = typename TypeParam::template map_type<int>;
  auto map = make_string_int_map<map_type>();
  auto const& const_map = map;

  auto value = get_optional_ref(const_map, "beta"sv);

  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(&value->get(), &const_map.find("beta")->second);

  static_assert(
      std::is_same_v<
          decltype(get_optional_ref(std::declval<map_type const&>(), "beta"sv)),
          std::optional<
              std::reference_wrapper<typename map_type::mapped_type const>>>);
}

TYPED_TEST(string_keyed_map_test,
           get_optional_ref_can_be_used_with_monadic_operations) {
  using map_type = typename TypeParam::template map_type<int>;
  auto map = make_string_int_map<map_type>();

  auto result = get_optional_ref(map, "beta"sv).transform([](auto ref) {
    return ref.get() + 5;
  });

  EXPECT_THAT(result, Optional(Eq(7)));
}

TYPED_TEST(string_keyed_map_test,
           get_optional_ref_supports_noncopyable_mapped_types) {
  using map_type = typename TypeParam::template map_type<std::unique_ptr<int>>;
  map_type map;
  map.emplace("answer", std::make_unique<int>(42));

  auto value = get_optional_ref(map, "answer"sv);

  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(&value->get(), &map.find("answer")->second);
  EXPECT_THAT(*value->get(), Eq(42));

  *value->get() = 43;

  EXPECT_THAT(*map.find("answer")->second, Eq(43));
}
