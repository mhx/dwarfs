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

#include <concepts>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <dwarfs/dense_map_index.h>
#include <dwarfs/internal/flat_dense_map_index.h>

#include "dense_index_test_helpers.h"

using namespace dwarfs;
using namespace dwarfs::test;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Optional;
using ::testing::Pair;

namespace {

template <typename T>
struct throwing_insert_policy : std_dense_map_index_policy<T> {
  template <typename Hash, typename Equal>
  using index_type = throwing_index_set<Hash, Equal>;
};

template <typename T>
struct append_only_store_policy : dense_map_index_policy_base<T> {
  using store_type = append_only_store<T>;

  template <typename Hash, typename Equal>
  using index_type = std::unordered_set<std::size_t, Hash, Equal>;
};

// A store that hands out entries by value rather than by reference.
template <typename T>
struct proxy_store_policy : dense_map_index_policy_base<T> {
  using store_type = proxy_store<T>;

  template <typename Hash, typename Equal>
  using index_type = std::unordered_set<std::size_t, Hash, Equal>;
};

template <typename T>
struct ascii_case_policy : dense_map_index_policy_base<T> {
  using hash_type = ascii_case_hash;
  using equal_type = ascii_case_equal;

  template <typename Hash, typename Equal>
  using index_type = std::unordered_set<std::size_t, Hash, Equal>;
};

// A probe type that compares equal to keys but cannot be turned into one, to
// pin down that lookups do not require key construction.
struct key_probe {
  std::string_view value;
};

struct key_probe_hash {
  using is_transparent = void;

  std::size_t operator()(std::string const& s) const noexcept {
    return std::hash<std::string_view>{}(s);
  }

  std::size_t operator()(key_probe p) const noexcept {
    return std::hash<std::string_view>{}(p.value);
  }
};

struct key_probe_equal {
  using is_transparent = void;

  bool
  operator()(std::string const& lhs, std::string const& rhs) const noexcept {
    return lhs == rhs;
  }

  bool operator()(std::string const& lhs, key_probe rhs) const noexcept {
    return lhs == rhs.value;
  }

  bool operator()(key_probe lhs, std::string const& rhs) const noexcept {
    return lhs.value == rhs;
  }
};

// Detection helpers. These have to be templates: a requires-expression over a
// concrete type is evaluated eagerly and would be a hard error rather than
// yielding false.
template <typename Index, typename K>
concept has_try_emplace =
    requires(Index& index, K const& key) { index.try_emplace(key, 1); };

template <typename Index, typename K>
concept has_mapped =
    requires(Index& index, K const& key) { index.mapped(key); };

template <typename Index, typename K>
concept has_insert_or_assign =
    requires(Index& index, K const& key) { index.insert_or_assign(key, 1); };

template <typename Index, typename K>
concept has_mutable_mapped_at = requires(Index& index, K const& key) {
  { index.mapped_at(key) } -> std::same_as<typename Index::mapped_type&>;
};

template <typename T>
struct key_probe_policy : dense_map_index_policy_base<T> {
  using hash_type = key_probe_hash;
  using equal_type = key_probe_equal;

  template <typename Hash, typename Equal>
  using index_type = std::unordered_set<std::size_t, Hash, Equal>;
};

} // namespace

using tested_policy_wrappers =
    ::testing::Types<policy_wrapper<std_dense_map_index_policy>,
                     policy_wrapper<internal::flat_dense_map_index_policy>>;

template <typename PolicyWrapper>
class dense_map_index_test : public ::testing::Test {
 protected:
  using index_type =
      basic_dense_map_index<std::string, int, PolicyWrapper::template policy>;

  std::vector<std::pair<std::string, int>> store;
  index_type index{store};
};

TYPED_TEST_SUITE(dense_map_index_test, tested_policy_wrappers);

TYPED_TEST(dense_map_index_test, starts_empty) {
  EXPECT_TRUE(this->index.empty());
  EXPECT_EQ(this->index.size(), 0);
  EXPECT_THAT(this->store, ElementsAre());
}

TYPED_TEST(dense_map_index_test, assigns_dense_indices_to_new_keys) {
  auto const first = this->index.emplace("alpha", 1);
  auto const second = this->index.emplace("beta", 2);

  EXPECT_THAT(first.index, Eq(0));
  EXPECT_TRUE(first.inserted);
  EXPECT_THAT(second.index, Eq(1));
  EXPECT_TRUE(second.inserted);

  EXPECT_EQ(this->index.size(), 2);
  EXPECT_THAT(this->store, ElementsAre(Pair("alpha", 1), Pair("beta", 2)));
}

TYPED_TEST(dense_map_index_test,
           duplicate_emplace_returns_existing_index_and_keeps_mapped_value) {
  auto const first = this->index.emplace("alpha", 1);
  auto const duplicate = this->index.emplace(std::string{"alpha"}, 99);

  EXPECT_THAT(first.index, Eq(0));
  EXPECT_TRUE(first.inserted);
  EXPECT_THAT(duplicate.index, Eq(0));
  EXPECT_FALSE(duplicate.inserted);

  EXPECT_EQ(this->index.size(), 1);
  EXPECT_THAT(this->store, ElementsAre(Pair("alpha", 1)));
}

TYPED_TEST(dense_map_index_test, supports_heterogeneous_key_lookup) {
  this->index.add("alpha", 1);
  this->index.add("beta", 2);

  std::string const alpha = "alpha";
  std::string_view const beta = "beta";
  std::string_view const missing = "missing";

  EXPECT_TRUE(this->index.contains(alpha));
  EXPECT_TRUE(this->index.contains(beta));
  EXPECT_FALSE(this->index.contains(missing));

  EXPECT_THAT(this->index.index_of(alpha), Optional(Eq(0)));
  EXPECT_THAT(this->index.index_of(beta), Optional(Eq(1)));
  EXPECT_THAT(this->index.index_of(missing), Eq(std::nullopt));
}

TYPED_TEST(dense_map_index_test, exposes_keys_and_mapped_values_by_index) {
  this->index.add("alpha", 1);
  this->index.add("beta", 2);

  EXPECT_THAT(this->index.mapped("alpha"), Eq(1));
  EXPECT_THAT(this->index.mapped_at("beta"), Eq(2));
  EXPECT_THAT(this->index[0], Pair("alpha", 1));
  EXPECT_THAT(this->index.at(1), Pair("beta", 2));
}

TYPED_TEST(dense_map_index_test, mapped_values_are_mutable_in_place) {
  this->index.add("alpha", 1);
  this->index.add("beta", 2);

  this->index.mapped("alpha") = 42;
  this->index.mapped_at("beta") += 10;

  EXPECT_THAT(this->store, ElementsAre(Pair("alpha", 42), Pair("beta", 12)));

  // ... and the index is unaffected by mapped value mutation
  EXPECT_THAT(this->index.index_of(std::string_view{"alpha"}), Optional(Eq(0)));
  EXPECT_THAT(this->index.index_of(std::string_view{"beta"}), Optional(Eq(1)));
}

TYPED_TEST(dense_map_index_test, mapped_creates_new_entry_if_key_is_absent) {
  this->index.mapped("alpha") = 42;

  EXPECT_THAT(this->index.index_of(std::string_view{"alpha"}), Optional(Eq(0)));
  EXPECT_THAT(this->store, ElementsAre(Pair("alpha", 42)));
}

TYPED_TEST(dense_map_index_test, at_throws_for_out_of_range_index) {
  this->index.add("alpha", 1);

  EXPECT_THROW(static_cast<void>(this->index.at(1)), std::out_of_range);
  EXPECT_THROW(static_cast<void>(this->index.mapped_at("beta")),
               std::out_of_range);
}

TYPED_TEST(dense_map_index_test, try_emplace_inserts_when_key_is_absent) {
  auto const first = this->index.try_emplace(std::string_view{"alpha"}, 1);
  auto const second = this->index.try_emplace(std::string_view{"beta"}, 2);

  EXPECT_THAT(first.index, Eq(0));
  EXPECT_TRUE(first.inserted);
  EXPECT_THAT(second.index, Eq(1));
  EXPECT_TRUE(second.inserted);

  EXPECT_THAT(this->store, ElementsAre(Pair("alpha", 1), Pair("beta", 2)));
}

TYPED_TEST(dense_map_index_test, try_emplace_keeps_existing_mapped_value) {
  this->index.try_emplace(std::string_view{"alpha"}, 1);

  auto const duplicate = this->index.try_emplace(std::string_view{"alpha"}, 99);

  EXPECT_THAT(duplicate.index, Eq(0));
  EXPECT_FALSE(duplicate.inserted);
  EXPECT_THAT(this->store, ElementsAre(Pair("alpha", 1)));
}

TYPED_TEST(dense_map_index_test, insert_or_assign_overwrites_mapped_value) {
  auto const first = this->index.insert_or_assign(std::string_view{"alpha"}, 1);
  auto const overwritten =
      this->index.insert_or_assign(std::string_view{"alpha"}, 2);
  auto const fresh = this->index.insert_or_assign(std::string_view{"beta"}, 3);

  EXPECT_THAT(first.index, Eq(0));
  EXPECT_TRUE(first.inserted);
  EXPECT_THAT(overwritten.index, Eq(0));
  EXPECT_FALSE(overwritten.inserted);
  EXPECT_THAT(fresh.index, Eq(1));
  EXPECT_TRUE(fresh.inserted);

  EXPECT_EQ(this->index.size(), 2);
  EXPECT_THAT(this->store, ElementsAre(Pair("alpha", 2), Pair("beta", 3)));
}

TYPED_TEST(dense_map_index_test, add_is_a_thin_wrapper_over_emplace) {
  EXPECT_THAT(this->index.add("alpha", 1), Eq(0));
  EXPECT_THAT(this->index.add("beta", 2), Eq(1));
  EXPECT_THAT(this->index.add("alpha", 3), Eq(0));

  EXPECT_THAT(this->store, ElementsAre(Pair("alpha", 1), Pair("beta", 2)));
}

TYPED_TEST(dense_map_index_test, reserve_preserves_behavior) {
  this->index.reserve(64);

  EXPECT_EQ(this->index.size(), 0);

  EXPECT_THAT(this->index.add("alpha", 1), Eq(0));
  EXPECT_THAT(this->index.add("beta", 2), Eq(1));
  EXPECT_THAT(this->index.add("alpha", 3), Eq(0));

  EXPECT_THAT(this->store, ElementsAre(Pair("alpha", 1), Pair("beta", 2)));
}

TYPED_TEST(dense_map_index_test, values_returns_the_external_store_contents) {
  this->index.add("alpha", 1);

  EXPECT_EQ(&this->index.values(), &this->store);
}

TYPED_TEST(dense_map_index_test, can_rebuild_index_from_existing_unique_store) {
  std::vector<std::pair<std::string, int>> rebuilt_store = {
      {"alpha", 1}, {"beta", 2}, {"gamma", 3}};

  basic_dense_map_index<std::string, int, TypeParam::template policy>
      rebuilt_index(rebuilt_store);

  EXPECT_EQ(rebuilt_index.size(), 3);
  EXPECT_THAT(rebuilt_index.mapped("gamma"), Eq(3));
  EXPECT_THAT(rebuilt_index.index_of(std::string_view{"beta"}),
              Optional(Eq(1)));
}

TYPED_TEST(dense_map_index_test, rejects_duplicate_keys_in_existing_store) {
  std::vector<std::pair<std::string, int>> rebuilt_store = {
      {"alpha", 1}, {"beta", 2}, {"alpha", 3}};

  EXPECT_THROW(
      (basic_dense_map_index<std::string, int, TypeParam::template policy>(
          rebuilt_store)),
      std::invalid_argument);
}

class counted_value_index_test : public ::testing::Test {
 protected:
  using index_type = basic_dense_map_index<std::string, counted_value,
                                           std_dense_map_index_policy>;

  void SetUp() override { counted_value::constructions = 0; }

  std::vector<std::pair<std::string, counted_value>> store;
  index_type index{store};
};

TEST_F(counted_value_index_test,
       try_emplace_does_not_construct_mapped_value_for_existing_key) {
  ASSERT_TRUE(index.try_emplace(std::string_view{"alpha"}, 1).inserted);

  auto const before = counted_value::constructions;
  auto const duplicate = index.try_emplace(std::string_view{"alpha"}, 99);

  EXPECT_FALSE(duplicate.inserted);
  EXPECT_THAT(duplicate.index, Eq(0));
  EXPECT_EQ(counted_value::constructions, before);
  EXPECT_THAT(index.mapped("alpha").value, Eq(1));
}

TEST_F(counted_value_index_test, emplace_constructs_and_discards_a_duplicate) {
  ASSERT_TRUE(index.emplace("alpha", counted_value{1}).inserted);

  auto const before = counted_value::constructions;
  auto const duplicate = index.emplace("alpha", counted_value{99});

  EXPECT_FALSE(duplicate.inserted);
  EXPECT_GT(counted_value::constructions, before);
  EXPECT_THAT(index.mapped("alpha").value, Eq(1));
  EXPECT_EQ(store.size(), 1);
}

class throwing_insert_map_index_test : public ::testing::Test {
 protected:
  using index_type =
      basic_dense_map_index<std::string, int, throwing_insert_policy>;

  void SetUp() override {
    throwing_index_set_control::fail_next_insert = false;
  }

  std::vector<std::pair<std::string, int>> store;
  index_type index{store};
};

TEST_F(throwing_insert_map_index_test,
       emplace_has_strong_guarantee_when_index_insertion_throws) {
  EXPECT_THAT(index.add("alpha", 1), Eq(0));

  throwing_index_set_control::fail_next_insert = true;
  EXPECT_THROW(static_cast<void>(index.emplace("beta", 2)), std::runtime_error);

  EXPECT_EQ(index.size(), 1);
  EXPECT_THAT(store, ElementsAre(Pair("alpha", 1)));
  EXPECT_THAT(index.index_of(std::string_view{"beta"}), Eq(std::nullopt));
}

TEST_F(throwing_insert_map_index_test,
       try_emplace_has_strong_guarantee_when_index_insertion_throws) {
  EXPECT_THAT(index.add("alpha", 1), Eq(0));

  throwing_index_set_control::fail_next_insert = true;
  EXPECT_THROW(
      static_cast<void>(index.try_emplace(std::string_view{"beta"}, 2)),
      std::runtime_error);

  EXPECT_EQ(index.size(), 1);
  EXPECT_THAT(store, ElementsAre(Pair("alpha", 1)));
  EXPECT_THAT(index.index_of(std::string_view{"beta"}), Eq(std::nullopt));
}

class append_only_map_index_test : public ::testing::Test {
 protected:
  using index_type =
      basic_dense_map_index<std::string, int, append_only_store_policy>;

  append_only_store<std::pair<std::string, int>> store;
  index_type index{store};
};

TEST_F(append_only_map_index_test, inserts_using_append_only_store) {
  auto const first = index.emplace("alpha", 1);
  auto const duplicate = index.emplace("alpha", 99);
  auto const second = index.try_emplace(std::string_view{"beta"}, 2);
  auto const duplicate2 = index.try_emplace(std::string_view{"alpha"}, 99);

  EXPECT_THAT(first.index, Eq(0));
  EXPECT_TRUE(first.inserted);
  EXPECT_THAT(duplicate.index, Eq(0));
  EXPECT_FALSE(duplicate.inserted);
  EXPECT_THAT(second.index, Eq(1));
  EXPECT_TRUE(second.inserted);
  EXPECT_THAT(duplicate2.index, Eq(0));
  EXPECT_FALSE(duplicate2.inserted);

  EXPECT_EQ(index.size(), 2);
  EXPECT_THAT(store.values(), ElementsAre(Pair("alpha", 1), Pair("beta", 2)));
}

TEST_F(append_only_map_index_test, mapped_values_are_mutable) {
  index.add("alpha", 1);
  index.mapped("alpha") = 7;

  EXPECT_THAT(store.values(), ElementsAre(Pair("alpha", 7)));
  EXPECT_THAT(index.index_of(std::string_view{"alpha"}), Optional(Eq(0)));
}

TEST_F(append_only_map_index_test, insert_or_assign_works_without_pop_back) {
  EXPECT_TRUE(index.insert_or_assign(std::string_view{"alpha"}, 1).inserted);
  EXPECT_FALSE(index.insert_or_assign(std::string_view{"alpha"}, 2).inserted);

  EXPECT_EQ(index.size(), 1);
  EXPECT_THAT(store.values(), ElementsAre(Pair("alpha", 2)));
}

TEST(dense_map_index_accessor_type_test, keys_are_never_exposed_mutably) {
  using map_index =
      basic_dense_map_index<std::string, int, std_dense_map_index_policy>;

  static_assert(std::same_as<map_index::key_type, std::string>);
  static_assert(std::same_as<map_index::mapped_type, int>);
  static_assert(
      std::same_as<map_index::value_type, std::pair<std::string, int>>);

  static_assert(std::same_as<map_index::const_reference,
                             std::pair<std::string, int> const&>);
  static_assert(std::same_as<decltype(std::declval<map_index&>()[0]),
                             std::pair<std::string, int> const&>);
  static_assert(std::same_as<decltype(std::declval<map_index&>().at(0)),
                             std::pair<std::string, int> const&>);

  static_assert(
      std::same_as<decltype(std::declval<map_index&>().mapped("")), int&>);
  static_assert(
      std::same_as<decltype(std::declval<map_index&>().mapped_at("")), int&>);
  static_assert(
      std::same_as<decltype(std::declval<map_index const&>().mapped_at("")),
                   int const&>);
}

TEST(dense_map_index_accessor_type_test, accessors_alias_the_store) {
  std::vector<std::pair<std::string, int>> store{{"alpha", 1}, {"beta", 2}};
  basic_dense_map_index<std::string, int, std_dense_map_index_policy> index{
      store};

  EXPECT_EQ(&index[0], &store[0]);
  EXPECT_EQ(&index.mapped("beta"), &store[1].second);
  EXPECT_EQ(&index.mapped_at("alpha"), &store[0].second);
}

TYPED_TEST(dense_map_index_test, lookup_on_empty_index) {
  EXPECT_FALSE(this->index.contains(std::string_view{"alpha"}));
  EXPECT_THAT(this->index.index_of(std::string_view{"alpha"}),
              Eq(std::nullopt));
  EXPECT_THROW(static_cast<void>(this->index.mapped_at("alpha")),
               std::out_of_range);
}

TYPED_TEST(dense_map_index_test, try_emplace_default_constructs_mapped_value) {
  auto const first = this->index.try_emplace(std::string_view{"alpha"});

  EXPECT_THAT(first.index, Eq(0));
  EXPECT_TRUE(first.inserted);
  EXPECT_THAT(this->store, ElementsAre(Pair("alpha", 0)));
}

TYPED_TEST(dense_map_index_test, mapped_returns_a_reference_into_the_store) {
  this->index.add("alpha", 1);

  auto& mapped = this->index.mapped("alpha");
  mapped = 7;

  EXPECT_THAT(this->store, ElementsAre(Pair("alpha", 7)));
  EXPECT_EQ(&mapped, &this->store[0].second);
}

TEST(dense_map_index_alias_test, convenience_alias_uses_the_std_policy) {
  static_assert(
      std::same_as<
          dense_map_index<std::string, int>,
          basic_dense_map_index<std::string, int, std_dense_map_index_policy>>);

  std::vector<std::pair<std::string, int>> store;
  dense_map_index<std::string, int> index{store};

  EXPECT_THAT(index.add("alpha", 1), Eq(0));
  EXPECT_THAT(index.add("alpha", 2), Eq(0));
  EXPECT_EQ(index.size(), 1);
}

TEST(dense_map_index_size_test, index_size_in_bytes_tracks_index_capacity) {
  std::vector<std::pair<std::string, int>> store;
  internal::flat_dense_map_index<std::string, int> index{store};

  index.reserve(1024);

  EXPECT_GT(index.index_size_in_bytes(), 1024 * sizeof(std::size_t) / 2);
}

class throwing_mapped_index_test : public ::testing::Test {
 protected:
  using index_type = basic_dense_map_index<std::string, throwing_value,
                                           std_dense_map_index_policy>;

  void SetUp() override { throwing_value::reset(); }
  void TearDown() override { throwing_value::reset(); }

  std::vector<std::pair<std::string, throwing_value>> store;
  index_type index{store};
};

TEST_F(throwing_mapped_index_test,
       emplace_has_strong_guarantee_when_mapped_construction_throws) {
  ASSERT_TRUE(index.emplace("alpha", throwing_value{1}).inserted);

  throwing_value::fail_construction = true;
  EXPECT_THROW(static_cast<void>(index.emplace("beta", throwing_value{2})),
               std::runtime_error);
  throwing_value::reset();

  EXPECT_EQ(index.size(), 1);
  EXPECT_EQ(store.size(), 1);
  EXPECT_THAT(index.index_of(std::string_view{"beta"}), Eq(std::nullopt));
  EXPECT_THAT(index.mapped_at("alpha").value, Eq(1));
}

TEST_F(throwing_mapped_index_test,
       try_emplace_has_strong_guarantee_when_mapped_construction_throws) {
  ASSERT_TRUE(index.try_emplace(std::string_view{"alpha"}, 1).inserted);

  throwing_value::fail_construction = true;
  EXPECT_THROW(
      static_cast<void>(index.try_emplace(std::string_view{"beta"}, 2)),
      std::runtime_error);
  throwing_value::reset();

  EXPECT_EQ(index.size(), 1);
  EXPECT_EQ(store.size(), 1);
  EXPECT_THAT(index.index_of(std::string_view{"beta"}), Eq(std::nullopt));
}

TEST_F(throwing_insert_map_index_test,
       insert_or_assign_has_strong_guarantee_when_index_insertion_throws) {
  EXPECT_THAT(index.add("alpha", 1), Eq(0));

  throwing_index_set_control::fail_next_insert = true;
  EXPECT_THROW(
      static_cast<void>(index.insert_or_assign(std::string_view{"beta"}, 2)),
      std::runtime_error);

  EXPECT_EQ(index.size(), 1);
  EXPECT_THAT(store, ElementsAre(Pair("alpha", 1)));
}

TEST_F(throwing_insert_map_index_test, mapped_propagates_insertion_failures) {
  EXPECT_THAT(index.add("alpha", 1), Eq(0));

  throwing_index_set_control::fail_next_insert = true;
  EXPECT_THROW(static_cast<void>(index.mapped("beta")), std::runtime_error);

  EXPECT_EQ(index.size(), 1);
  EXPECT_THAT(store, ElementsAre(Pair("alpha", 1)));
}

TEST_F(append_only_map_index_test, mapped_inserts_without_pop_back) {
  index.add("alpha", 1);

  index.mapped("beta") = 5;

  EXPECT_EQ(index.size(), 2);
  EXPECT_THAT(store.values(), ElementsAre(Pair("alpha", 1), Pair("beta", 5)));
  EXPECT_THAT(index.index_of(std::string_view{"beta"}), Optional(Eq(1)));
}

class case_insensitive_map_index_test : public ::testing::Test {
 protected:
  using index_type = basic_dense_map_index<std::string, int, ascii_case_policy>;

  std::vector<std::pair<std::string, int>> store;
  index_type index{store};
};

TEST_F(case_insensitive_map_index_test, honours_a_custom_key_policy) {
  EXPECT_TRUE(index.emplace("Alpha", 1).inserted);
  EXPECT_FALSE(index.emplace("ALPHA", 2).inserted);
  EXPECT_FALSE(index.try_emplace(std::string_view{"alpha"}, 3).inserted);

  EXPECT_EQ(index.size(), 1);
  EXPECT_THAT(store, ElementsAre(Pair("Alpha", 1)));
  EXPECT_THAT(index.mapped_at(std::string_view{"aLpHa"}), Eq(1));

  index.insert_or_assign(std::string_view{"ALPHA"}, 9);

  EXPECT_THAT(store, ElementsAre(Pair("Alpha", 9)));
}

class key_probe_map_index_test : public ::testing::Test {
 protected:
  using index_type = basic_dense_map_index<std::string, int, key_probe_policy>;

  std::vector<std::pair<std::string, int>> store;
  index_type index{store};
};

TEST_F(key_probe_map_index_test, lookups_do_not_require_key_construction) {
  static_assert(!std::constructible_from<std::string, key_probe const&>);

  index.add("alpha", 1);
  index.add("beta", 2);

  EXPECT_TRUE(index.contains(key_probe{"alpha"}));
  EXPECT_THAT(index.index_of(key_probe{"beta"}), Optional(Eq(1)));
  EXPECT_THAT(index.mapped_at(key_probe{"alpha"}), Eq(1));

  index.mapped_at(key_probe{"beta"}) = 7;
  EXPECT_THAT(store, ElementsAre(Pair("alpha", 1), Pair("beta", 7)));

  EXPECT_THROW(static_cast<void>(index.mapped_at(key_probe{"gamma"})),
               std::out_of_range);

  // operations that would have to build a key are not available...
  static_assert(!has_try_emplace<index_type, key_probe>);
  static_assert(!has_mapped<index_type, key_probe>);
  static_assert(!has_insert_or_assign<index_type, key_probe>);

  // ... but the plain string versions are
  static_assert(has_try_emplace<index_type, std::string>);
  static_assert(has_mapped<index_type, std::string>);
  static_assert(has_insert_or_assign<index_type, std::string>);
}

class proxy_store_map_index_test : public ::testing::Test {
 protected:
  using index_type =
      basic_dense_map_index<std::string, int, proxy_store_policy>;

  proxy_store<std::pair<std::string, int>> store;
  index_type index{store};
};

TEST_F(proxy_store_map_index_test, supports_read_only_access_by_value) {
  static_assert(
      std::same_as<index_type::const_reference, std::pair<std::string, int>>);
  static_assert(std::same_as<index_type::const_mapped_reference, int>);
  static_assert(
      std::same_as<decltype(std::declval<index_type&>().mapped_at("")), int>);

  EXPECT_THAT(index.add("alpha", 1), Eq(0));
  EXPECT_THAT(index.add("beta", 2), Eq(1));
  EXPECT_THAT(index.add("alpha", 3), Eq(0));

  EXPECT_EQ(index.size(), 2);
  EXPECT_EQ(store.size(), 2);
  EXPECT_THAT(index[1], Pair("beta", 2));
  EXPECT_THAT(index.at(0), Pair("alpha", 1));
  EXPECT_THAT(index.mapped_at(std::string_view{"beta"}), Eq(2));
  EXPECT_THROW(static_cast<void>(index.at(2)), std::out_of_range);
}

TEST_F(proxy_store_map_index_test, in_place_mutation_is_not_available) {
  // There is nothing to mutate in place when the store hands out values, so
  // these must not compile rather than silently write to a temporary.
  static_assert(!has_mapped<index_type, std::string>);
  static_assert(!has_insert_or_assign<index_type, std::string>);
  static_assert(!has_mutable_mapped_at<index_type, std::string>);

  // Duplicate insertion still rolls the store back.
  EXPECT_TRUE(index.emplace("alpha", 1).inserted);
  EXPECT_FALSE(index.emplace("alpha", 2).inserted);
  EXPECT_EQ(store.size(), 1);
}
