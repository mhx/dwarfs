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

#include <bitset>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <dwarfs/thrift_lite/field_ref.h>

namespace {

using testing::HasSubstr;
using testing::Throws;
using testing::ThrowsMessage;

namespace tl = dwarfs::thrift_lite;

struct widget {
  int x{0};
  void inc() { ++x; }
};

template <typename R>
concept can_assign_int = requires(R r) { r = 1; };

template <typename R>
concept can_reset = requires(R r) { r.reset(); };

template <typename R>
concept can_emplace_int = requires(R r) { r.emplace(1); };

template <typename R>
concept has_to_optional = requires(R const& r) { r.to_optional(); };

template <typename R>
concept has_value_or_unique_ptr =
    requires(R const& r) { r.value_or(std::unique_ptr<int>{}); };

static_assert(std::is_same_v<tl::field_ref<int&>::value_type, int>);
static_assert(std::is_same_v<tl::field_ref<int const&>::value_type, int>);
static_assert(
    std::is_same_v<
        tl::optional_field_ref<int&, std::bitset<1>::reference>::value_type,
        int>);
static_assert(
    std::is_same_v<tl::optional_field_ref<int const&, bool>::value_type, int>);

static_assert(std::is_constructible_v<tl::field_ref<int&>, int&>);
static_assert(std::is_constructible_v<tl::field_ref<int const&>, int const&>);

static_assert(!can_assign_int<tl::field_ref<int const&>>);
static_assert(!can_reset<tl::optional_field_ref<int const&, bool>>);
static_assert(!can_emplace_int<tl::optional_field_ref<int const&, bool>>);
static_assert(!can_assign_int<tl::optional_field_ref<int const&, bool>>);

using move_only_opt_ref =
    tl::optional_field_ref<std::unique_ptr<int>&, std::bitset<1>::reference>;
static_assert(!has_to_optional<move_only_opt_ref>);
static_assert(!has_value_or_unique_ptr<move_only_opt_ref>);

} // namespace

TEST(field_ref, wraps_mutable_reference_and_supports_assignment_and_deref) {
  auto w = widget{.x = 1};

  auto ref = tl::field_ref<widget&>{w};
  EXPECT_EQ(ref->x, 1);

  ref->inc();
  EXPECT_EQ(w.x, 2);

  (*ref).x = 10;
  EXPECT_EQ(w.x, 10);

  ref = widget{.x = 42};
  EXPECT_EQ(w.x, 42);
}

TEST(field_ref, wraps_const_reference_and_supports_read_only_access) {
  auto const w = widget{.x = 7};

  auto ref = tl::field_ref<widget const&>{w};
  EXPECT_EQ(ref->x, 7);
  EXPECT_EQ((*ref).x, 7);
}

TEST(field_ref, preserves_rvalue_reference_qualifier) {
  auto v = std::make_unique<int>(123);

  auto ref = tl::field_ref<std::unique_ptr<int>&&>{v};
  static_assert(std::is_same_v<decltype(ref.value()), std::unique_ptr<int>&&>);

  ASSERT_TRUE(v);
  EXPECT_EQ(*v, 123);

  auto moved = ref.value();
  EXPECT_FALSE(v);
  ASSERT_TRUE(moved);
  EXPECT_EQ(*moved, 123);
}

TEST(field_ref, assignment_from_const_lvalue) {
  auto w = widget{.x = 1};
  auto const new_value = widget{.x = 42};

  auto ref = tl::field_ref<widget&>{w};
  ref = new_value;

  EXPECT_EQ(w.x, 42);
}

TEST(optional_field_ref, preserves_rvalue_reference_qualifier) {
  auto v = std::make_unique<int>(123);
  auto isset = std::bitset<1>{};
  isset[0] = true;

  auto ref =
      tl::optional_field_ref<std::unique_ptr<int>&&, std::bitset<1>::reference>{
          v, isset[0]};
  static_assert(std::is_same_v<decltype(ref.value()), std::unique_ptr<int>&&>);

  ASSERT_TRUE(v);
  EXPECT_EQ(*v, 123);

  auto moved = ref.value();
  EXPECT_FALSE(v);
  ASSERT_TRUE(moved);
  EXPECT_EQ(*moved, 123);
}

TEST(optional_field_ref, unset_state_behaves_like_empty_optional) {
  auto v = 0;
  auto isset = std::bitset<1>{};
  isset.reset();

  auto ref =
      tl::optional_field_ref<int&, std::bitset<1>::reference>{v, isset[0]};

  EXPECT_FALSE(ref.has_value());
  EXPECT_FALSE(static_cast<bool>(ref));
  EXPECT_EQ(ref.value_or(99), 99);
  EXPECT_FALSE(ref.to_optional().has_value());

  EXPECT_THAT([&] { ref.value(); }, Throws<std::bad_optional_access>());
  EXPECT_THAT([&] { *ref; }, Throws<std::bad_optional_access>());
  EXPECT_THAT([&] { ref.operator->(); }, Throws<std::bad_optional_access>());
}

TEST(optional_field_ref, assignment_sets_bit_and_updates_value) {
  auto v = 0;
  auto isset = std::bitset<1>{};
  isset.reset();

  auto ref =
      tl::optional_field_ref<int&, std::bitset<1>::reference>{v, isset[0]};

  ref = 7;

  EXPECT_TRUE(ref.has_value());
  EXPECT_TRUE(isset.test(0));
  EXPECT_EQ(v, 7);
  EXPECT_EQ(ref.value(), 7);
  EXPECT_EQ(*ref, 7);

  auto opt = ref.to_optional();
  ASSERT_TRUE(opt.has_value());
  EXPECT_EQ(*opt, 7);

  EXPECT_EQ(ref.value_or(123), 7);
}

TEST(optional_field_ref,
     emplace_sets_bit_assigns_constructed_value_and_returns_reference) {
  auto v = 1;
  auto isset = std::bitset<1>{};
  isset.reset();

  auto ref =
      tl::optional_field_ref<int&, std::bitset<1>::reference>{v, isset[0]};

  auto& r = ref.emplace(99);

  EXPECT_TRUE(ref.has_value());
  EXPECT_TRUE(isset.test(0));
  EXPECT_EQ(&r, &v);
  EXPECT_EQ(v, 99);
  EXPECT_EQ(ref.value(), 99);
}

TEST(optional_field_ref, reset_clears_bit_and_resets_underlying_value) {
  auto v = 123;
  auto isset = std::bitset<1>{};
  isset.set(0);

  auto ref =
      tl::optional_field_ref<int&, std::bitset<1>::reference>{v, isset[0]};

  ASSERT_TRUE(ref.has_value());
  ref.reset();

  EXPECT_FALSE(ref.has_value());
  EXPECT_FALSE(isset.test(0));
  EXPECT_EQ(v, 0);

  EXPECT_THAT([&] { ref.value(); }, Throws<std::bad_optional_access>());
}

TEST(optional_field_ref, operator_arrow_works_for_class_types_when_set) {
  auto v = widget{.x = 5};
  auto isset = std::bitset<1>{};
  isset.reset();

  auto ref =
      tl::optional_field_ref<widget&, std::bitset<1>::reference>{v, isset[0]};

  ref.emplace(widget{.x = 10});
  ref->inc();
  EXPECT_EQ(v.x, 11);
}

TEST(optional_field_ref, const_view_uses_bool_bitref_and_is_read_only) {
  auto v = 321;
  auto isset = std::bitset<1>{};
  isset.set(0);

  auto ref = tl::optional_field_ref<int const&, bool>{v, isset.test(0)};

  EXPECT_TRUE(ref.has_value());
  EXPECT_EQ(ref.value(), 321);

  auto opt = ref.to_optional();
  ASSERT_TRUE(opt.has_value());
  EXPECT_EQ(*opt, 321);

  EXPECT_EQ(ref.value_or(7), 321);
}

TEST(optional_field_ref, const_view_unset_throws_bad_optional_access) {
  auto v = 1;
  auto isset = std::bitset<1>{};
  isset.reset();

  auto ref = tl::optional_field_ref<int const&, bool>{v, isset.test(0)};

  EXPECT_FALSE(ref.has_value());

  EXPECT_THAT([&] { ref.value(); }, Throws<std::bad_optional_access>());
}

TEST(optional_field_ref, works_with_string_assignment_and_value_or_copy) {
  auto s = std::string{};
  auto isset = std::bitset<1>{};
  isset.reset();

  auto ref = tl::optional_field_ref<std::string&, std::bitset<1>::reference>{
      s, isset[0]};

  ref = "hello";
  EXPECT_TRUE(ref.has_value());
  EXPECT_EQ(s, "hello");
  EXPECT_EQ(ref.value(), "hello");

  EXPECT_EQ(ref.value_or("x"), "hello");

  ref.reset();
  EXPECT_FALSE(ref.has_value());
  EXPECT_EQ(ref.value_or("fallback"), "fallback");
}

TEST(optional_field_ref, assignment_from_const_lvalue) {
  auto s = std::string{"initial"};
  auto const new_value = std::string{"updated"};
  auto isset = std::bitset<1>{};
  isset.reset();

  auto ref = tl::optional_field_ref<std::string&, std::bitset<1>::reference>{
      s, isset[0]};

  ref = new_value;

  EXPECT_TRUE(ref.has_value());
  EXPECT_EQ(s, "updated");
}

TEST(optional_field_ref, assignment_from_compatible_non_array_rvalue) {
  auto s = std::string{"initial"};
  auto isset = std::bitset<1>{};
  isset.reset();

  auto ref = tl::optional_field_ref<std::string&, std::bitset<1>::reference>{
      s, isset[0]};

  ref = std::string_view{"moved"};

  EXPECT_TRUE(ref.has_value());
  EXPECT_EQ(s, "moved");
}
