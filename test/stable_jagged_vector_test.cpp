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

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <dwarfs/container/stable_jagged_vector.h>

namespace dwarfs::container {

namespace {

using namespace std::string_view_literals;
using testing::ElementsAre;

static_assert(
    std::same_as<stable_jagged_vector<char>::value_type, std::string_view>);
static_assert(std::same_as<stable_jagged_vector<char8_t>::value_type,
                           std::u8string_view>);
static_assert(std::same_as<stable_jagged_vector<char16_t>::value_type,
                           std::u16string_view>);
static_assert(std::same_as<stable_jagged_vector<std::uint32_t>::value_type,
                           std::span<std::uint32_t const>>);

static_assert(std::ranges::random_access_range<stable_jagged_vector<char>>);
static_assert(
    std::ranges::random_access_range<stable_jagged_vector<char> const>);

} // namespace

TEST(stable_jagged_vector, empty) {
  stable_jagged_vector<char> vec;

  EXPECT_TRUE(vec.empty());
  EXPECT_EQ(0, vec.size());
  EXPECT_EQ(vec.begin(), vec.end());
  EXPECT_EQ(0, vec.block_count());
  EXPECT_EQ(0, vec.payload_size_in_bytes());
  EXPECT_EQ(0, vec.payload_capacity_in_bytes());
  EXPECT_LE(vec.size_in_bytes(), vec.capacity_in_bytes());
}

TEST(stable_jagged_vector, character_values_are_string_views) {
  stable_jagged_vector<char8_t> vec;

  vec.emplace_back(u8"usr");
  vec.emplace_back(std::u8string{u8"local"});
  vec.push_back(u8"bin"sv);
  vec.emplace_back(u8"", 0);

  EXPECT_EQ(4, vec.size());
  EXPECT_EQ(u8"usr"sv, std::as_const(vec).front());
  EXPECT_EQ(u8"local"sv, std::as_const(vec).at(1));
  EXPECT_EQ(u8"bin"sv, std::as_const(vec)[2]);
  EXPECT_TRUE(std::as_const(vec).back().empty());

  EXPECT_EQ(3, std::as_const(vec)[0].size());

  EXPECT_THAT(std::as_const(vec),
              ElementsAre(u8"usr"sv, u8"local"sv, u8"bin"sv, u8""sv));
}

TEST(stable_jagged_vector, non_character_values_are_spans) {
  stable_jagged_vector<std::uint32_t, 64> vec;

  std::array<std::uint32_t, 4> a{1, 2, 3, 4};
  std::vector<std::uint32_t> b{10, 20, 30};
  vec.emplace_back(a);
  vec.emplace_back(b);

  auto const first = std::as_const(vec)[0];
  auto const second = std::as_const(vec)[1];

  EXPECT_THAT(first, ElementsAre(1, 2, 3, 4));
  EXPECT_THAT(second, ElementsAre(10, 20, 30));
  static_assert(std::is_const_v<std::remove_reference_t<decltype(first[0])>>);
}

TEST(stable_jagged_vector, backing_addresses_are_stable) {
  // use a tiny block size to force storage growth
  stable_jagged_vector<char, 4> vec;
  vec.emplace_back("abc"sv);

  auto const original = std::as_const(vec)[0];
  auto const* original_data = original.data();

  for (int i = 0; i < 1000; ++i) {
    vec.emplace_back("wxyz"sv);
  }

  EXPECT_EQ("abc"sv, std::as_const(vec)[0]);
  EXPECT_EQ(original_data, std::as_const(vec)[0].data());
  EXPECT_GT(vec.block_count(), 1);
}

TEST(stable_jagged_vector, elements_never_cross_regular_block_boundaries) {
  stable_jagged_vector<char, 16> vec;

  vec.emplace_back("0123456789"sv); // block 0, bytes [0, 10)
  vec.emplace_back("abcdefghij"sv); // does not fit: block 1
  vec.emplace_back("xyz"sv);        // still fits in block 1

  EXPECT_EQ(2, vec.block_count());
  EXPECT_EQ(23, vec.payload_size_in_bytes());
  EXPECT_EQ(32, vec.payload_capacity_in_bytes());
  EXPECT_THAT(std::as_const(vec),
              ElementsAre("0123456789"sv, "abcdefghij"sv, "xyz"sv));
}

TEST(stable_jagged_vector, oversized_element_gets_dedicated_block) {
  stable_jagged_vector<char, 16> vec;

  std::string large(100, 'x');
  vec.emplace_back(large);
  vec.emplace_back("tail"sv);

  EXPECT_EQ(2, vec.block_count());
  EXPECT_EQ(104, vec.payload_size_in_bytes());
  EXPECT_EQ(116, vec.payload_capacity_in_bytes());
  EXPECT_EQ(std::string_view{large}, std::as_const(vec)[0]);
  EXPECT_EQ("tail"sv, std::as_const(vec)[1]);
}

TEST(stable_jagged_vector, swapping_elements_only_swaps_descriptors) {
  stable_jagged_vector<char, 16> vec;
  vec.emplace_back("usr"sv);
  vec.emplace_back("local"sv);

  auto const* usr_data = std::as_const(vec)[0].data();
  auto const* local_data = std::as_const(vec)[1].data();
  auto const payload_size = vec.payload_size_in_bytes();
  auto const block_count = vec.block_count();

  using std::swap;
  swap(vec[0], vec[1]);

  EXPECT_EQ("local"sv, std::as_const(vec)[0]);
  EXPECT_EQ("usr"sv, std::as_const(vec)[1]);
  EXPECT_EQ(local_data, std::as_const(vec)[0].data());
  EXPECT_EQ(usr_data, std::as_const(vec)[1].data());
  EXPECT_EQ(payload_size, vec.payload_size_in_bytes());
  EXPECT_EQ(block_count, vec.block_count());
}

TEST(stable_jagged_vector, proxy_assignment_shares_payload_within_container) {
  stable_jagged_vector<char> vec;
  vec.emplace_back("alpha"sv);
  vec.emplace_back("beta"sv);

  auto const* alpha_data = std::as_const(vec)[0].data();
  auto const payload_size = vec.payload_size_in_bytes();

  vec[1] = vec[0];

  EXPECT_EQ("alpha"sv, std::as_const(vec)[1]);
  EXPECT_EQ(alpha_data, std::as_const(vec)[1].data());
  EXPECT_EQ(payload_size, vec.payload_size_in_bytes());
}

TEST(stable_jagged_vector, assigning_external_value_appends_immutable_payload) {
  stable_jagged_vector<char> vec;
  vec.emplace_back("old"sv);

  auto const old = std::as_const(vec)[0];
  auto const* old_data = old.data();

  vec[0] = "replacement"sv;

  EXPECT_EQ("replacement"sv, std::as_const(vec)[0]);
  EXPECT_NE(old_data, std::as_const(vec)[0].data());
  // existing payload is untouched
  EXPECT_EQ("old"sv, old);
}

TEST(stable_jagged_vector, resize_can_shrink_or_grow_with_empty_elements) {
  stable_jagged_vector<char> vec;
  vec.emplace_back("one"sv);
  vec.emplace_back("two"sv);
  vec.emplace_back("three"sv);

  auto const payload_size = vec.payload_size_in_bytes();

  vec.resize(1);
  ASSERT_EQ(1, vec.size());
  EXPECT_EQ("one"sv, std::as_const(vec)[0]);
  // shrinking descriptors does not reclaim payload
  EXPECT_EQ(payload_size, vec.payload_size_in_bytes());

  vec.resize(4);
  EXPECT_EQ(4, vec.size());
  EXPECT_EQ("one"sv, std::as_const(vec)[0]);
  EXPECT_TRUE(std::as_const(vec)[1].empty());
  EXPECT_TRUE(std::as_const(vec)[2].empty());
  EXPECT_TRUE(std::as_const(vec)[3].empty());
  EXPECT_EQ(payload_size, vec.payload_size_in_bytes());
}

TEST(stable_jagged_vector, clear_releases_payload_blocks) {
  stable_jagged_vector<char, 8> vec;
  vec.emplace_back("12345678"sv);
  vec.emplace_back("abcdefgh"sv);

  ASSERT_EQ(2, vec.block_count());
  ASSERT_GT(vec.payload_capacity_in_bytes(), 0);

  vec.clear();

  EXPECT_TRUE(vec.empty());
  EXPECT_EQ(0, vec.block_count());
  EXPECT_EQ(0, vec.payload_size_in_bytes());
  EXPECT_EQ(0, vec.payload_capacity_in_bytes());
}

TEST(stable_jagged_vector, at_throws) {
  stable_jagged_vector<char> vec;
  vec.emplace_back("x"sv);

  EXPECT_NO_THROW((void)std::as_const(vec).at(0));
  EXPECT_THROW((void)std::as_const(vec).at(1), std::out_of_range);
  EXPECT_THROW((void)vec.at(1), std::out_of_range);
}

TEST(stable_jagged_vector, container_swap_preserves_backing_addresses) {
  stable_jagged_vector<char> a;
  stable_jagged_vector<char> b;
  a.emplace_back("left"sv);
  b.emplace_back("right"sv);

  auto const* left = std::as_const(a)[0].data();
  auto const* right = std::as_const(b)[0].data();

  using std::swap;
  swap(a, b);

  EXPECT_EQ("right"sv, std::as_const(a)[0]);
  EXPECT_EQ("left"sv, std::as_const(b)[0]);
  EXPECT_EQ(right, std::as_const(a)[0].data());
  EXPECT_EQ(left, std::as_const(b)[0].data());
}

TEST(stable_jagged_vector, move_leaves_source_empty_and_preserves_addresses) {
  stable_jagged_vector<char> source;
  source.emplace_back("stable"sv);
  auto const* data = std::as_const(source)[0].data();

  stable_jagged_vector<char> target{std::move(source)};

  EXPECT_TRUE(source.empty());
  EXPECT_EQ(0, source.block_count());
  ASSERT_EQ(1, target.size());
  EXPECT_EQ("stable"sv, std::as_const(target)[0]);
  EXPECT_EQ(data, std::as_const(target)[0].data());
}

TEST(stable_jagged_vector, memory_accounting_is_monotonic_with_allocations) {
  stable_jagged_vector<char, 16> vec;

  auto const empty_capacity = vec.capacity_in_bytes();
  vec.emplace_back("abc"sv);
  auto const first_size = vec.size_in_bytes();
  auto const first_capacity = vec.capacity_in_bytes();

  EXPECT_GE(first_size, 3);
  EXPECT_GE(first_capacity, first_size);
  EXPECT_GT(first_capacity, empty_capacity);

  vec.emplace_back("0123456789abcdef"sv);
  EXPECT_GT(vec.size_in_bytes(), first_size);
  EXPECT_GE(vec.capacity_in_bytes(), vec.size_in_bytes());
}

} // namespace dwarfs::container
