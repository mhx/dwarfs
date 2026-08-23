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
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <ranges>
#include <span>
#include <stdexcept>
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
using testing::HasSubstr;
using testing::ThrowsMessage;

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

using char_vector = stable_jagged_vector<char>;

static_assert(!std::assignable_from<char_vector::reference&, std::string_view>);
static_assert(
    !std::indirectly_writable<char_vector::iterator, std::string_view>);
static_assert(!std::permutable<char_vector::iterator>);

static_assert(std::is_swappable_v<char_vector::reference>);

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

TEST(stable_jagged_vector, oversized_element_does_not_strand_the_fill_block) {
  stable_jagged_vector<char, 16> vec;
  std::string const large(40, 'x');

  vec.emplace_back("abc"sv);
  vec.emplace_back(large);
  vec.emplace_back("def"sv);

  EXPECT_EQ(2, vec.block_count());
  EXPECT_EQ(46, vec.payload_size_in_bytes());
  EXPECT_EQ(56, vec.payload_capacity_in_bytes());

  auto const first = std::as_const(vec)[0];
  auto const third = std::as_const(vec)[2];
  EXPECT_EQ(first.data() + first.size(), third.data());

  EXPECT_THAT(std::as_const(vec),
              ElementsAre("abc"sv, std::string_view{large}, "def"sv));
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

TEST(stable_jagged_vector, resize_can_shrink_or_grow_with_empty_elements) {
  stable_jagged_vector<char> vec;
  vec.emplace_back("one"sv);
  vec.emplace_back("two"sv);
  vec.emplace_back("three"sv);

  auto const capacity = vec.payload_capacity_in_bytes();

  vec.resize(1);
  ASSERT_EQ(1, vec.size());
  EXPECT_EQ("one"sv, std::as_const(vec)[0]);
  EXPECT_EQ(3, vec.payload_size_in_bytes());
  EXPECT_EQ(capacity, vec.payload_capacity_in_bytes());

  vec.resize(4);
  EXPECT_EQ(4, vec.size());
  EXPECT_EQ("one"sv, std::as_const(vec)[0]);
  EXPECT_TRUE(std::as_const(vec)[1].empty());
  EXPECT_TRUE(std::as_const(vec)[2].empty());
  EXPECT_TRUE(std::as_const(vec)[3].empty());
  EXPECT_EQ(3, vec.payload_size_in_bytes());
  EXPECT_EQ(capacity, vec.payload_capacity_in_bytes());
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

  // the fill block must have been reset along with the storage
  vec.emplace_back("x"sv);
  EXPECT_EQ(1, vec.block_count());
  EXPECT_EQ("x"sv, std::as_const(vec)[0]);
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

TEST(stable_jagged_vector, move_assignment_releases_target) {
  stable_jagged_vector<char> source;
  stable_jagged_vector<char> target;
  source.emplace_back("source"sv);
  target.emplace_back("target"sv);

  auto const* data = std::as_const(source)[0].data();

  target = std::move(source);

  EXPECT_TRUE(source.empty());
  EXPECT_EQ(0, source.block_count());
  EXPECT_EQ(0, source.payload_size_in_bytes());

  ASSERT_EQ(1, target.size());
  EXPECT_EQ("source"sv, std::as_const(target)[0]);
  EXPECT_EQ(data, std::as_const(target)[0].data());
  EXPECT_EQ(1, target.block_count());
  EXPECT_EQ(6, target.payload_size_in_bytes());

  // self-move is a no-op
  auto* self = &target;
  target = std::move(*self);
  ASSERT_EQ(1, target.size());
  EXPECT_EQ("source"sv, std::as_const(target)[0]);
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

TEST(stable_jagged_vector, mutable_proxy_observers) {
  stable_jagged_vector<char> vec;
  vec.emplace_back("alpha"sv);
  vec.emplace_back(""sv);

  auto proxy = vec[0];

  std::string_view const converted = proxy;

  EXPECT_EQ("alpha"sv, converted);
  EXPECT_EQ("alpha"sv, proxy.load());
  EXPECT_EQ(5, proxy.size());
  EXPECT_FALSE(proxy.empty());
  EXPECT_EQ('a', proxy[0]);
  EXPECT_EQ('h', proxy[3]);
  EXPECT_EQ(converted.data(), proxy.data());
  EXPECT_EQ(5, std::distance(proxy.begin(), proxy.end()));
  EXPECT_TRUE(std::equal(proxy.begin(), proxy.end(), converted.begin()));

  EXPECT_TRUE(vec[1].empty());
  EXPECT_EQ(0, vec[1].size());
}

TEST(stable_jagged_vector, mutable_accessors_return_proxies) {
  stable_jagged_vector<char> vec;
  vec.emplace_back("first"sv);
  vec.emplace_back("middle"sv);
  vec.emplace_back("last"sv);

  EXPECT_EQ("first"sv, vec.front().load());
  EXPECT_EQ("last"sv, vec.back().load());
  EXPECT_EQ("middle"sv, vec.at(1).load());

  using std::swap;
  swap(vec.front(), vec.back());

  EXPECT_THAT(std::as_const(vec), ElementsAre("last"sv, "middle"sv, "first"sv));
}

TEST(stable_jagged_vector, emplace_back_rejects_null_data) {
  stable_jagged_vector<char> vec;

  EXPECT_THROW((void)vec.emplace_back(nullptr, 1), std::invalid_argument);
  EXPECT_TRUE(vec.empty());

  // a null pointer with zero length is a valid empty element
  EXPECT_NO_THROW((void)vec.emplace_back(nullptr, 0));
  ASSERT_EQ(1, vec.size());
  EXPECT_TRUE(std::as_const(vec)[0].empty());
  EXPECT_EQ(0, vec.block_count());
}

TEST(stable_jagged_vector, emplace_back_rejects_overflowing_length) {
  stable_jagged_vector<std::uint32_t> vec;
  std::array<std::uint32_t, 1> data{42};

  static_assert(stable_jagged_vector<std::uint32_t>::max_element_size ==
                std::numeric_limits<std::size_t>::max() /
                    sizeof(std::uint32_t));

  constexpr auto too_long =
      stable_jagged_vector<std::uint32_t>::max_element_size + 1;

  EXPECT_THROW((void)vec.emplace_back(data.data(), too_long),
               std::length_error);
  EXPECT_TRUE(vec.empty());
  EXPECT_EQ(0, vec.block_count());
  EXPECT_EQ(0, vec.payload_size_in_bytes());
}

TEST(stable_jagged_vector, const_and_reverse_iterators) {
  stable_jagged_vector<char> vec;
  vec.emplace_back("a"sv);
  vec.emplace_back("bb"sv);
  vec.emplace_back("ccc"sv);

  EXPECT_THAT(std::vector(vec.cbegin(), vec.cend()),
              ElementsAre("a"sv, "bb"sv, "ccc"sv));
  EXPECT_THAT(std::vector(vec.crbegin(), vec.crend()),
              ElementsAre("ccc"sv, "bb"sv, "a"sv));
  EXPECT_THAT(
      std::vector(std::as_const(vec).rbegin(), std::as_const(vec).rend()),
      ElementsAre("ccc"sv, "bb"sv, "a"sv));

  EXPECT_EQ(3, std::distance(vec.rbegin(), vec.rend()));
  EXPECT_EQ("ccc"sv, (*vec.rbegin()).load());
  EXPECT_EQ("bb"sv, vec.begin()[1].load());
}

TEST(stable_jagged_vector, iter_swap_exchanges_descriptors_only) {
  stable_jagged_vector<char> vec;
  vec.emplace_back("first"sv);
  vec.emplace_back("second"sv);

  auto const* first = std::as_const(vec)[0].data();
  auto const* second = std::as_const(vec)[1].data();
  auto const payload = vec.payload_size_in_bytes();
  auto const capacity = vec.payload_capacity_in_bytes();

  std::ranges::iter_swap(vec.begin(), vec.begin() + 1);

  EXPECT_EQ("second"sv, std::as_const(vec)[0]);
  EXPECT_EQ("first"sv, std::as_const(vec)[1]);
  EXPECT_EQ(second, std::as_const(vec)[0].data());
  EXPECT_EQ(first, std::as_const(vec)[1].data());
  EXPECT_EQ(payload, vec.payload_size_in_bytes());
  EXPECT_EQ(capacity, vec.payload_capacity_in_bytes());
}

TEST(stable_jagged_vector, swap_based_algorithms_do_not_copy_payload) {
  stable_jagged_vector<char, 16> vec;
  vec.emplace_back("a"sv);
  vec.emplace_back("bb"sv);
  vec.emplace_back("ccc"sv);

  auto const payload = vec.payload_size_in_bytes();
  auto const capacity = vec.payload_capacity_in_bytes();

  std::reverse(vec.begin(), vec.end());

  EXPECT_THAT(std::as_const(vec), ElementsAre("ccc"sv, "bb"sv, "a"sv));
  EXPECT_EQ(payload, vec.payload_size_in_bytes());
  EXPECT_EQ(capacity, vec.payload_capacity_in_bytes());
}

TEST(stable_jagged_vector, capacity_management_preserves_contents) {
  stable_jagged_vector<char, 16> vec;
  vec.reserve(64);

  for (int i = 0; i < 100; ++i) {
    vec.emplace_back("payload"sv);
  }

  auto const* first = std::as_const(vec)[0].data();
  auto const blocks = vec.block_count();
  auto const payload = vec.payload_size_in_bytes();

  vec.shrink_to_fit();
  vec.optimize_storage();

  ASSERT_EQ(100, vec.size());
  EXPECT_EQ(blocks, vec.block_count());
  EXPECT_EQ(payload, vec.payload_size_in_bytes());
  EXPECT_EQ(first, std::as_const(vec)[0].data());
  EXPECT_EQ("payload"sv, std::as_const(vec)[0]);
  EXPECT_EQ("payload"sv, std::as_const(vec)[99]);
  EXPECT_LE(vec.size_in_bytes(), vec.capacity_in_bytes());
}

TEST(stable_jagged_vector, supports_all_character_types) {
  stable_jagged_vector<wchar_t> wide;
  wide.emplace_back(L"wide");
  EXPECT_EQ(std::wstring_view{L"wide"}, std::as_const(wide)[0]);
  EXPECT_EQ(4, std::as_const(wide)[0].size());

  stable_jagged_vector<char16_t> u16;
  u16.emplace_back(u"utf16");
  EXPECT_EQ(std::u16string_view{u"utf16"}, std::as_const(u16)[0]);

  stable_jagged_vector<char32_t> u32;
  u32.emplace_back(U"utf32");
  EXPECT_EQ(std::u32string_view{U"utf32"}, std::as_const(u32)[0]);

  std::array<char, 3> const raw{'a', 'b', 'c'};
  stable_jagged_vector<char> narrow;
  narrow.emplace_back(raw);
  EXPECT_EQ("abc"sv, std::as_const(narrow)[0]);
}

TEST(stable_jagged_vector, cross_container_swap_is_not_supported) {
  stable_jagged_vector<char> vec1;
  stable_jagged_vector<char> vec2;

  vec1.emplace_back("first"sv);
  vec2.emplace_back("second"sv);

  EXPECT_THAT(
      [&] {
        using std::swap;
        swap(vec1[0], vec2[0]);
      },
      ThrowsMessage<std::invalid_argument>(
          HasSubstr("cross-container swap is not supported")));
}

} // namespace dwarfs::container
