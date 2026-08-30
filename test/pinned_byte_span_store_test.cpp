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
#include <cstddef>
#include <cstdint>
#include <map>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <dwarfs/container/dense_value_index.h>
#include <dwarfs/container/pinned_byte_span_store.h>

namespace {

using namespace dwarfs::container;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Optional;

template <typename Container>
using mutable_at_result_t =
    decltype(std::declval<Container&>().at(std::declval<std::size_t>()));

template <typename Container>
using const_at_result_t =
    decltype(std::declval<Container const&>().at(std::declval<std::size_t>()));

template <typename Container>
using mutable_subscript_result_t =
    decltype(std::declval<Container&>()[std::declval<std::size_t>()]);

template <typename Container>
using const_subscript_result_t =
    decltype(std::declval<Container const&>()[std::declval<std::size_t>()]);

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
static_assert(std::same_as<mutable_subscript_result_t<test_container>,
                           std::span<std::byte>>);
static_assert(std::same_as<const_subscript_result_t<test_container>,
                           std::span<std::byte const>>);

static_assert(detail::byte_range<std::span<std::byte const>>);
static_assert(detail::byte_range<std::vector<std::byte>>);
static_assert(detail::byte_range<std::vector<unsigned char>>);
static_assert(detail::byte_range<std::string_view>);
static_assert(detail::byte_range<std::array<std::byte, 4>>);
static_assert(!detail::byte_range<std::vector<unsigned>>);
static_assert(!detail::byte_range<std::vector<bool>>);

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

template <std::size_t ChunkSize>
struct pinned_byte_span_index_policy_holder {
  template <typename T>
  struct policy {
    static_assert(std::same_as<T, std::span<std::byte const>>);

    using store_type = pinned_byte_span_store<ChunkSize>;
    using hash_type = byte_span_hash;
    using equal_type = byte_span_equal;

    template <typename Hash, typename Equal>
    using index_type = std::unordered_set<std::size_t, Hash, Equal>;
  };
};

template <std::size_t ChunkSize>
using pinned_byte_span_index = basic_dense_value_index<
    std::span<std::byte const>,
    pinned_byte_span_index_policy_holder<ChunkSize>::template policy>;

using pbsi2_type = pinned_byte_span_index<2>;

static_assert(
    std::same_as<pbsi2_type::const_reference, std::span<std::byte const>>);
static_assert(std::same_as<const_subscript_result_t<pbsi2_type>,
                           std::span<std::byte const>>);
static_assert(std::same_as<mutable_subscript_result_t<pbsi2_type>,
                           std::span<std::byte const>>);
static_assert(
    std::same_as<const_at_result_t<pbsi2_type>, std::span<std::byte const>>);

std::string span_to_string(std::span<std::byte const> s) {
  std::string out(s.size(), '\0');
  for (std::size_t i = 0; i < s.size(); ++i) {
    out[i] = static_cast<char>(s[i]);
  }
  return out;
}

} // namespace

TEST(pinned_byte_span_store_test,
     construction_reports_span_size_and_starts_empty) {
  test_container v{7};

  EXPECT_EQ(v.span_size(), 7);
  EXPECT_EQ(v.size(), 0);
}

TEST(pinned_byte_span_store_test, construction_rejects_zero_span_size) {
  EXPECT_THROW(test_container{0}, std::invalid_argument);
  EXPECT_NO_THROW(test_container{1});
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

TEST(pinned_byte_span_store_test, empty_reflects_size) {
  test_container v{4};

  EXPECT_TRUE(v.empty());

  set_bytes(v.emplace_back(), {1, 2, 3, 4});
  EXPECT_FALSE(v.empty());

  v.pop_back();
  EXPECT_TRUE(v.empty());
}

TEST(pinned_byte_span_store_test, subscript_gives_unchecked_access) {
  test_container v{3};
  set_bytes(v.emplace_back(), {1, 2, 3});
  set_bytes(v.emplace_back(), {4, 5, 6});

  EXPECT_EQ(v[0].data(), v.at(0).data());
  EXPECT_EQ(v[1].data(), v.at(1).data());

  v[1][0] = std::byte{99};
  EXPECT_THAT(to_uints(v.at(1)), ElementsAre(99u, 5u, 6u));

  test_container const& cv = v;
  EXPECT_THAT(to_uints(cv[0]), ElementsAre(1u, 2u, 3u));
}

TEST(pinned_byte_span_store_test, reserve_preallocates_without_changing_size) {
  using small_chunk_container = pinned_byte_span_store<2>;

  small_chunk_container v{4};

  EXPECT_EQ(v.capacity(), 0);

  v.reserve(5);

  EXPECT_EQ(v.size(), 0);
  EXPECT_TRUE(v.empty());
  EXPECT_GE(v.capacity(), 5);
  EXPECT_EQ(v.capacity() % 2, 0);

  auto const previous_capacity = v.capacity();

  for (unsigned i = 0; i < 5; ++i) {
    set_bytes(v.emplace_back(), {static_cast<unsigned char>(i), 0, 0, 0});
  }

  EXPECT_EQ(v.size(), 5);
  EXPECT_EQ(v.capacity(), previous_capacity);
  EXPECT_THAT(to_uints(v.at(4)), ElementsAre(4u, 0u, 0u, 0u));

  v.reserve(1);
  EXPECT_EQ(v.capacity(), previous_capacity);
}

TEST(pinned_byte_span_store_test, emplace_back_copies_byte_range_contents) {
  test_container v{4};

  std::vector<std::byte> const source{std::byte{1}, std::byte{2}, std::byte{3},
                                      std::byte{4}};

  auto const stored = v.emplace_back(source);

  ASSERT_EQ(v.size(), 1);
  EXPECT_EQ(stored.data(), v.at(0).data());
  EXPECT_THAT(to_uints(v.at(0)), ElementsAre(1u, 2u, 3u, 4u));

  // the store owns a copy; mutating it must not affect the source
  v[0][0] = std::byte{9};
  EXPECT_THAT(to_uints(v.at(0)), ElementsAre(9u, 2u, 3u, 4u));
  EXPECT_EQ(std::to_integer<unsigned>(source[0]), 1u);
}

TEST(pinned_byte_span_store_test, emplace_back_accepts_any_byte_range) {
  test_container v{4};

  std::vector<unsigned char> const uchars{1, 2, 3, 4};
  std::array<std::byte, 4> const arr{std::byte{5}, std::byte{6}, std::byte{7},
                                     std::byte{8}};
  std::string_view const sv{"WXYZ"};

  v.emplace_back(uchars);
  v.emplace_back(arr);
  v.emplace_back(sv);
  v.emplace_back(v.at(0)); // std::span<std::byte const>

  ASSERT_EQ(v.size(), 4);
  EXPECT_THAT(to_uints(v.at(0)), ElementsAre(1u, 2u, 3u, 4u));
  EXPECT_THAT(to_uints(v.at(1)), ElementsAre(5u, 6u, 7u, 8u));
  EXPECT_THAT(to_uints(v.at(2)), ElementsAre('W', 'X', 'Y', 'Z'));
  EXPECT_THAT(to_uints(v.at(3)), ElementsAre(1u, 2u, 3u, 4u));
}

TEST(pinned_byte_span_store_test, emplace_back_rejects_size_mismatch) {
  test_container v{4};

  set_bytes(v.emplace_back(), {1, 2, 3, 4});

  EXPECT_THROW(v.emplace_back(std::string_view{"abc"}), std::invalid_argument);
  EXPECT_THROW(v.emplace_back(std::string_view{"abcde"}),
               std::invalid_argument);
  EXPECT_THROW(v.emplace_back(std::string_view{}), std::invalid_argument);

  // strong guarantee: the store is unchanged and still usable
  EXPECT_EQ(v.size(), 1);
  EXPECT_THAT(to_uints(v.at(0)), ElementsAre(1u, 2u, 3u, 4u));
  EXPECT_NO_THROW(v.emplace_back(std::string_view{"abcd"}));
  EXPECT_EQ(v.size(), 2);
}

TEST(pinned_byte_span_store_test, pop_back_shrinks_and_reuses_storage) {
  using small_chunk_container = pinned_byte_span_store<2>;

  small_chunk_container v{4};

  v.emplace_back(std::string_view{"aaaa"});
  v.emplace_back(std::string_view{"bbbb"});

  auto const* second_ptr = v.at(1).data();
  auto const capacity_before = v.capacity();

  v.pop_back();

  EXPECT_EQ(v.size(), 1);
  EXPECT_THROW(static_cast<void>(v.at(1)), std::out_of_range);
  EXPECT_EQ(v.capacity(), capacity_before);

  // the slot is reused rather than reallocated
  v.emplace_back(std::string_view{"cccc"});

  EXPECT_EQ(v.size(), 2);
  EXPECT_EQ(v.at(1).data(), second_ptr);
  EXPECT_THAT(to_uints(v.at(1)), ElementsAre('c', 'c', 'c', 'c'));
}

TEST(pinned_byte_span_store_test, byte_span_hash_and_equal_are_content_based) {
  test_container v{4};

  v.emplace_back(std::string_view{"abcd"});
  v.emplace_back(std::string_view{"abcd"}); // deliberate duplicate content
  v.emplace_back(std::string_view{"abce"});

  byte_span_hash const hash;
  byte_span_equal const equal;

  EXPECT_NE(v.at(0).data(), v.at(1).data());
  EXPECT_EQ(hash(v.at(0)), hash(v.at(1)));
  EXPECT_TRUE(equal(v.at(0), v.at(1)));
  EXPECT_FALSE(equal(v.at(0), v.at(2)));

  // ... and consistent across byte range types
  std::vector<unsigned char> const uchars{'a', 'b', 'c', 'd'};
  EXPECT_EQ(hash(std::string_view{"abcd"}), hash(v.at(0)));
  EXPECT_EQ(hash(uchars), hash(v.at(0)));
  EXPECT_TRUE(equal(uchars, v.at(0)));

  // differing lengths must never compare equal
  EXPECT_FALSE(equal(std::string_view{"abc"}, v.at(0)));
  EXPECT_FALSE(equal(std::string_view{"abcde"}, v.at(0)));
}

class pinned_byte_span_index_test : public ::testing::Test {
 protected:
  static constexpr std::size_t kSpanSize = 4;

  using store_type = pinned_byte_span_store<2>;
  using index_type = pinned_byte_span_index<2>;

  store_type store{kSpanSize};
  index_type index{store};
};

TEST_F(pinned_byte_span_index_test, starts_empty) {
  EXPECT_TRUE(index.empty());
  EXPECT_EQ(index.size(), 0);
  EXPECT_FALSE(index.contains(std::string_view{"abcd"}));
  EXPECT_THAT(index.index_of(std::string_view{"abcd"}), Eq(std::nullopt));
}

TEST_F(pinned_byte_span_index_test, assigns_dense_indices_to_unique_spans) {
  EXPECT_THAT(index.add(std::string_view{"aaaa"}), Eq(0));
  EXPECT_THAT(index.add(std::string_view{"bbbb"}), Eq(1));
  EXPECT_THAT(index.add(std::string_view{"cccc"}), Eq(2));

  EXPECT_EQ(index.size(), 3);
  EXPECT_EQ(store.size(), 3);
  EXPECT_EQ(index.values().size_in_bytes(), 3 * kSpanSize);

  EXPECT_EQ(span_to_string(index[0]), "aaaa");
  EXPECT_EQ(span_to_string(index[1]), "bbbb");
  EXPECT_EQ(span_to_string(index.at(2)), "cccc");
}

TEST_F(pinned_byte_span_index_test,
       duplicate_span_is_stored_only_once_and_rolls_the_store_back) {
  auto const first = index.emplace(std::string_view{"abcd"});

  EXPECT_THAT(first.index, Eq(0));
  EXPECT_TRUE(first.inserted);
  EXPECT_EQ(store.size(), 1);

  auto const duplicate = index.emplace(std::string_view{"abcd"});

  EXPECT_THAT(duplicate.index, Eq(0));
  EXPECT_FALSE(duplicate.inserted);

  // the rolled back append must not leave a stale span behind
  EXPECT_EQ(store.size(), 1);
  EXPECT_EQ(index.size(), 1);
  EXPECT_EQ(store.size_in_bytes(), kSpanSize);
}

TEST_F(pinned_byte_span_index_test,
       deduplication_is_content_based_not_address_based) {
  std::vector<std::byte> const a{std::byte{1}, std::byte{2}, std::byte{3},
                                 std::byte{4}};
  std::vector<std::byte> const b = a; // equal content, distinct storage

  EXPECT_THAT(index.add(a), Eq(0));
  EXPECT_THAT(index.add(b), Eq(0));
  EXPECT_EQ(index.size(), 1);

  // and a span already owned by the store must map back onto itself
  EXPECT_THAT(index.add(index[0]), Eq(0));
  EXPECT_EQ(index.size(), 1);
}

TEST_F(pinned_byte_span_index_test, supports_heterogeneous_lookup) {
  index.add(std::string_view{"0123"});
  index.add(std::string_view{"4567"});

  std::vector<unsigned char> const uchars{'0', '1', '2', '3'};
  std::array<std::byte, 4> const arr{std::byte{'4'}, std::byte{'5'},
                                     std::byte{'6'}, std::byte{'7'}};

  EXPECT_THAT(index.index_of(std::string_view{"0123"}), Optional(Eq(0)));
  EXPECT_THAT(index.index_of(uchars), Optional(Eq(0)));
  EXPECT_THAT(index.index_of(arr), Optional(Eq(1)));
  EXPECT_THAT(index.index_of(index[1]), Optional(Eq(1)));
  EXPECT_THAT(index.index_of(std::string_view{"89ab"}), Eq(std::nullopt));

  EXPECT_TRUE(index.contains(uchars));
  EXPECT_FALSE(index.contains(std::string_view{"89ab"}));

  // a differently sized probe must never match a prefix or suffix
  EXPECT_FALSE(index.contains(std::string_view{"012"}));
  EXPECT_FALSE(index.contains(std::string_view{"01234"}));
}

TEST_F(pinned_byte_span_index_test,
       indexed_spans_stay_pinned_while_the_index_grows) {
  constexpr std::size_t kCount = 11; // spans several chunks of 2

  std::vector<std::byte const*> pointers;

  for (std::size_t i = 0; i < kCount; ++i) {
    auto value = std::string(kSpanSize, '\0');
    for (std::size_t k = 0; k < kSpanSize; ++k) {
      value[k] = static_cast<char>('a' + ((i >> (2 * k)) & 0x03));
    }

    auto const ix = index.add(value);
    ASSERT_THAT(ix, Eq(i));
    pointers.push_back(index[ix].data());
  }

  ASSERT_EQ(index.size(), kCount);

  for (std::size_t i = 0; i < kCount; ++i) {
    EXPECT_EQ(index[i].data(), pointers[i]) << "i=" << i;
    EXPECT_THAT(index.index_of(index[i]), Optional(Eq(i))) << "i=" << i;
  }
}

TEST_F(pinned_byte_span_index_test,
       size_mismatch_throws_and_leaves_both_containers_intact) {
  index.add(std::string_view{"aaaa"});

  EXPECT_THROW(index.add(std::string_view{"bbb"}), std::invalid_argument);
  EXPECT_THROW(index.add(std::string_view{"bbbbb"}), std::invalid_argument);

  EXPECT_EQ(index.size(), 1);
  EXPECT_EQ(store.size(), 1);
  EXPECT_THAT(index.index_of(std::string_view{"aaaa"}), Optional(Eq(0)));

  // still usable afterwards
  EXPECT_THAT(index.add(std::string_view{"bbbb"}), Eq(1));
}

TEST_F(pinned_byte_span_index_test, reserve_forwards_to_the_store) {
  index.reserve(9);

  EXPECT_EQ(index.size(), 0);
  EXPECT_EQ(store.size(), 0);
  EXPECT_GE(store.capacity(), 9);

  auto const capacity_before = store.capacity();

  EXPECT_THAT(index.add(std::string_view{"aaaa"}), Eq(0));
  EXPECT_THAT(index.add(std::string_view{"aaaa"}), Eq(0));

  EXPECT_EQ(index.size(), 1);
  EXPECT_EQ(store.capacity(), capacity_before);
}

TEST(pinned_byte_span_index_adoption_test, adopts_prepopulated_unique_store) {
  pinned_byte_span_store<2> store{4};

  set_bytes(store.emplace_back(), {'a', 'l', 'p', 'h'});
  set_bytes(store.emplace_back(), {'b', 'e', 't', 'a'});

  pinned_byte_span_index<2> index{store};

  EXPECT_EQ(index.size(), 2);
  EXPECT_THAT(index.index_of(std::string_view{"beta"}), Optional(Eq(1)));
  EXPECT_THAT(index.add(std::string_view{"beta"}), Eq(1));
  EXPECT_EQ(store.size(), 2);
  EXPECT_THAT(index.add(std::string_view{"gamm"}), Eq(2));
}

TEST(pinned_byte_span_index_adoption_test,
     rejects_prepopulated_store_with_duplicates) {
  pinned_byte_span_store<2> store{4};

  set_bytes(store.emplace_back(), {'a', 'l', 'p', 'h'});
  set_bytes(store.emplace_back(), {'a', 'l', 'p', 'h'});

  EXPECT_THROW((pinned_byte_span_index<2>{store}), std::invalid_argument);
}

TEST(pinned_byte_span_index_stress_test, matches_a_reference_implementation) {
  constexpr std::size_t kSpanSize = 4;
  constexpr std::size_t kIterations = 4000;

  pinned_byte_span_store<8> store{kSpanSize};
  pinned_byte_span_index<8> index{store};

  std::map<std::string, std::size_t> reference;
  std::mt19937_64 rng{0x5eed5eed5eed5eedULL};
  std::uniform_int_distribution<unsigned> dist{0, 5};

  for (std::size_t i = 0; i < kIterations; ++i) {
    std::string key(kSpanSize, '\0');
    for (auto& c : key) {
      c = static_cast<char>('a' + dist(rng));
    }

    auto const [ref_it, ref_inserted] =
        reference.emplace(key, reference.size());
    auto const result = index.emplace(key);

    ASSERT_EQ(ref_inserted, result.inserted) << "i=" << i;
    ASSERT_EQ(ref_it->second, result.index) << "i=" << i;
    ASSERT_EQ(reference.size(), index.size()) << "i=" << i;
    ASSERT_EQ(reference.size(), store.size()) << "i=" << i;
  }

  for (auto const& [key, ix] : reference) {
    ASSERT_THAT(index.index_of(key), Optional(Eq(ix)));
    ASSERT_EQ(span_to_string(index[ix]), key);
  }
}
