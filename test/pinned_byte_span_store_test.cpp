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
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <dwarfs/container/pinned_byte_span_store.h>

namespace {

using namespace dwarfs::container;
using ::testing::ElementsAre;

template <typename Container>
using mutable_at_result_t =
    decltype(std::declval<Container&>().at(std::declval<std::size_t>()));

template <typename Container>
using const_at_result_t =
    decltype(std::declval<Container const&>().at(std::declval<std::size_t>()));

using test_container = pinned_byte_span_store<4>;

static_assert(!std::is_copy_constructible_v<test_container>);
static_assert(!std::is_copy_assignable_v<test_container>);
static_assert(std::is_move_constructible_v<test_container>);
static_assert(std::is_move_assignable_v<test_container>);

static_assert(
    std::same_as<decltype(std::declval<test_container&>().emplace_back()),
                 std::span<std::byte>>);
static_assert(
    std::same_as<mutable_at_result_t<test_container>, std::span<std::byte>>);
static_assert(std::same_as<const_at_result_t<test_container>,
                           std::span<std::byte const>>);

std::vector<unsigned> to_uints(std::span<std::byte const> s) {
  std::vector<unsigned> out;
  out.reserve(s.size());
  for (auto b : s) {
    out.push_back(std::to_integer<unsigned>(b));
  }
  return out;
}

void set_bytes(std::span<std::byte> s,
               std::initializer_list<unsigned char> values) {
  ASSERT_EQ(s.size(), values.size());

  auto it = values.begin();
  for (std::size_t i = 0; i < s.size(); ++i, ++it) {
    s[i] = static_cast<std::byte>(*it);
  }
}

} // namespace

TEST(pinned_byte_span_store_test,
     construction_reports_span_size_and_starts_empty) {
  test_container v{7};

  EXPECT_EQ(v.span_size(), 7);
  EXPECT_EQ(v.size(), 0);
}

TEST(pinned_byte_span_store_test,
     emplace_back_returns_mutable_span_and_increments_size) {
  test_container v{5};

  auto s = v.emplace_back();

  EXPECT_EQ(v.size(), 1);
  EXPECT_EQ(s.size(), 5);

  set_bytes(s, {1, 2, 3, 4, 5});
  EXPECT_THAT(to_uints(v.at(0)), ElementsAre(1u, 2u, 3u, 4u, 5u));
}

TEST(pinned_byte_span_store_test,
     at_returns_mutable_span_into_existing_storage) {
  test_container v{4};
  auto s = v.emplace_back();
  set_bytes(s, {10, 20, 30, 40});

  auto t = v.at(0);
  t[1] = std::byte{99};
  t[3] = std::byte{77};

  EXPECT_THAT(to_uints(v.at(0)), ElementsAre(10u, 99u, 30u, 77u));
}

TEST(pinned_byte_span_store_test,
     const_at_returns_const_span_with_expected_contents) {
  test_container v{3};
  set_bytes(v.emplace_back(), {7, 8, 9});

  test_container const& cv = v;
  auto s = cv.at(0);

  EXPECT_THAT(to_uints(s), ElementsAre(7u, 8u, 9u));
}

TEST(pinned_byte_span_store_test, at_throws_for_out_of_range_indices) {
  test_container v{4};

  EXPECT_THROW(static_cast<void>(v.at(0)), std::out_of_range);

  set_bytes(v.emplace_back(), {1, 2, 3, 4});

  EXPECT_NO_THROW(static_cast<void>(v.at(0)));
  EXPECT_THROW(static_cast<void>(v.at(1)), std::out_of_range);

  test_container const& cv = v;
  EXPECT_THROW(static_cast<void>(cv.at(1)), std::out_of_range);
}

TEST(pinned_byte_span_store_test,
     elements_across_chunk_boundaries_are_accessible) {
  using small_chunk_container = pinned_byte_span_store<2>;

  small_chunk_container v{3};

  for (unsigned i = 0; i < 5; ++i) {
    auto s = v.emplace_back();
    set_bytes(s, {static_cast<unsigned char>(10 * i + 0),
                  static_cast<unsigned char>(10 * i + 1),
                  static_cast<unsigned char>(10 * i + 2)});
  }

  ASSERT_EQ(v.size(), 5);

  EXPECT_THAT(to_uints(v.at(0)), ElementsAre(0u, 1u, 2u));
  EXPECT_THAT(to_uints(v.at(1)), ElementsAre(10u, 11u, 12u));
  EXPECT_THAT(to_uints(v.at(2)), ElementsAre(20u, 21u, 22u));
  EXPECT_THAT(to_uints(v.at(3)), ElementsAre(30u, 31u, 32u));
  EXPECT_THAT(to_uints(v.at(4)), ElementsAre(40u, 41u, 42u));
}

TEST(pinned_byte_span_store_test, multiple_spans_are_independent) {
  test_container v{4};

  auto first = v.emplace_back();
  auto second = v.emplace_back();

  set_bytes(first, {1, 2, 3, 4});
  set_bytes(second, {5, 6, 7, 8});

  EXPECT_THAT(to_uints(v.at(0)), ElementsAre(1u, 2u, 3u, 4u));
  EXPECT_THAT(to_uints(v.at(1)), ElementsAre(5u, 6u, 7u, 8u));
}

TEST(pinned_byte_span_store_test, appended_spans_do_not_move_when_growing) {
  using small_chunk_container = pinned_byte_span_store<2>;

  small_chunk_container v{6};

  auto first = v.emplace_back();
  auto second = v.emplace_back();

  set_bytes(first, {1, 2, 3, 4, 5, 6});
  set_bytes(second, {7, 8, 9, 10, 11, 12});

  auto* first_ptr = first.data();
  auto* second_ptr = second.data();

  for (unsigned i = 0; i < 10; ++i) {
    auto s = v.emplace_back();
    set_bytes(s, {static_cast<unsigned char>(20 + i),
                  static_cast<unsigned char>(21 + i),
                  static_cast<unsigned char>(22 + i),
                  static_cast<unsigned char>(23 + i),
                  static_cast<unsigned char>(24 + i),
                  static_cast<unsigned char>(25 + i)});
  }

  EXPECT_EQ(v.at(0).data(), first_ptr);
  EXPECT_EQ(v.at(1).data(), second_ptr);

  EXPECT_THAT(to_uints(v.at(0)), ElementsAre(1u, 2u, 3u, 4u, 5u, 6u));
  EXPECT_THAT(to_uints(v.at(1)), ElementsAre(7u, 8u, 9u, 10u, 11u, 12u));
}

TEST(pinned_byte_span_store_test,
     move_construction_preserves_contents_and_empties_source) {
  test_container src{4};

  set_bytes(src.emplace_back(), {1, 2, 3, 4});
  set_bytes(src.emplace_back(), {5, 6, 7, 8});

  auto* first_ptr = src.at(0).data();
  auto* second_ptr = src.at(1).data();

  test_container dst{std::move(src)};

  EXPECT_EQ(src.span_size(), 4);
  EXPECT_EQ(src.size(), 0);

  EXPECT_EQ(dst.span_size(), 4);
  ASSERT_EQ(dst.size(), 2);

  EXPECT_EQ(dst.at(0).data(), first_ptr);
  EXPECT_EQ(dst.at(1).data(), second_ptr);
  EXPECT_THAT(to_uints(dst.at(0)), ElementsAre(1u, 2u, 3u, 4u));
  EXPECT_THAT(to_uints(dst.at(1)), ElementsAre(5u, 6u, 7u, 8u));
}

TEST(pinned_byte_span_store_test,
     move_assignment_preserves_contents_and_empties_source) {
  test_container src{3};
  set_bytes(src.emplace_back(), {9, 8, 7});
  set_bytes(src.emplace_back(), {6, 5, 4});

  auto* first_ptr = src.at(0).data();
  auto* second_ptr = src.at(1).data();

  test_container dst{11};
  set_bytes(dst.emplace_back(), {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1});

  dst = std::move(src);

  EXPECT_EQ(src.span_size(), 3);
  EXPECT_EQ(src.size(), 0);

  EXPECT_EQ(dst.span_size(), 3);
  ASSERT_EQ(dst.size(), 2);

  EXPECT_EQ(dst.at(0).data(), first_ptr);
  EXPECT_EQ(dst.at(1).data(), second_ptr);
  EXPECT_THAT(to_uints(dst.at(0)), ElementsAre(9u, 8u, 7u));
  EXPECT_THAT(to_uints(dst.at(1)), ElementsAre(6u, 5u, 4u));
}

TEST(pinned_byte_span_store_test, returned_span_points_at_same_storage_as_at) {
  test_container v{4};

  auto s = v.emplace_back();
  auto* p = s.data();

  set_bytes(s, {42, 43, 44, 45});

  EXPECT_EQ(v.at(0).data(), p);
  EXPECT_THAT(to_uints(v.at(0)), ElementsAre(42u, 43u, 44u, 45u));
}
