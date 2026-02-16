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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <dwarfs/error.h>
#include <dwarfs/option_map.h>

using namespace dwarfs;

using ::testing::HasSubstr;
using ::testing::Not;
using ::testing::ThrowsMessage;

namespace {

auto throws_runtime_error(std::string const& core_message) {
  return ThrowsMessage<runtime_error>(HasSubstr(core_message));
}

} // namespace

TEST(option_map, ctor_parses_choice_only) {
  auto om = option_map("choice");

  EXPECT_EQ(om.choice(), "choice");
  EXPECT_FALSE(om.has_options());
  EXPECT_NO_THROW(om.report());
}

TEST(option_map, ctor_parses_choice_and_options) {
  auto om = option_map("choice:a=1:b=two:c");

  EXPECT_EQ(om.choice(), "choice");
  EXPECT_TRUE(om.has_options());
}

TEST(option_map, ctor_rejects_duplicate_options) {
  EXPECT_THAT([] { option_map("choice:a=1:a=2"); },
              throws_runtime_error("duplicate option 'a' for choice 'choice'"));

  EXPECT_THAT([] { option_map("choice:a:a=2"); },
              throws_runtime_error("duplicate option 'a' for choice 'choice'"));

  EXPECT_THAT([] { option_map("choice:a=1:a"); },
              throws_runtime_error("duplicate option 'a' for choice 'choice'"));
}

TEST(option_map, get_required_value_returns_value_when_present) {
  auto om = option_map("choice:key=123");

  EXPECT_EQ(om.get<int>("key"), 123);

  EXPECT_FALSE(om.has_options());
  EXPECT_NO_THROW(om.report());
}

TEST(option_map, get_required_value_throws_when_absent) {
  auto om = option_map("choice");

  EXPECT_THAT([&] { om.get<int>("missing"); },
              throws_runtime_error(
                  "missing value for option 'missing' of choice 'choice'"));

  EXPECT_FALSE(om.has_options());
  EXPECT_NO_THROW(om.report());
}

TEST(option_map, get_required_value_throws_when_present_without_value) {
  auto om = option_map("choice:key");

  EXPECT_THAT([&] { om.get<int>("key"); },
              throws_runtime_error(
                  "missing value for option 'key' of choice 'choice'"));
}

TEST(option_map, get_with_default_returns_default_when_absent) {
  auto om = option_map("choice");

  EXPECT_EQ(om.get<int>("key", 7), 7);

  EXPECT_FALSE(om.has_options());
  EXPECT_NO_THROW(om.report());
}

TEST(option_map, get_with_default_returns_value_when_present_with_value) {
  auto om = option_map("choice:key=9");

  EXPECT_EQ(om.get<int>("key", 7), 9);

  EXPECT_FALSE(om.has_options());
  EXPECT_NO_THROW(om.report());
}

TEST(option_map, get_with_default_throws_when_present_without_value) {
  auto om = option_map("choice:key");

  EXPECT_THAT([&] { om.get<int>("key", 7); },
              throws_runtime_error(
                  "missing value for option 'key' of choice 'choice'"));
}

TEST(option_map, get_string_required_returns_value_when_present) {
  auto om = option_map("choice:name=alice");

  EXPECT_EQ(om.get<std::string>("name"), "alice");

  EXPECT_FALSE(om.has_options());
  EXPECT_NO_THROW(om.report());
}

TEST(option_map,
     get_string_required_returns_empty_string_for_empty_string_value) {
  auto om = option_map("choice:empty=");

  EXPECT_EQ(om.get<std::string>("empty"), "");

  EXPECT_FALSE(om.has_options());
  EXPECT_NO_THROW(om.report());
}

TEST(option_map, get_string_with_default_returns_value_even_if_empty_string) {
  auto om = option_map("choice:empty=");

  EXPECT_EQ(om.get<std::string>("empty", std::string{"fallback"}), "");

  EXPECT_FALSE(om.has_options());
  EXPECT_NO_THROW(om.report());
}

TEST(option_map, get_bool_returns_false_when_absent) {
  auto om = option_map("choice");

  EXPECT_FALSE(om.get<bool>("flag"));

  EXPECT_FALSE(om.has_options());
  EXPECT_NO_THROW(om.report());
}

TEST(option_map, get_bool_returns_true_when_present_without_value) {
  auto om = option_map("choice:flag");

  EXPECT_TRUE(om.get<bool>("flag"));

  EXPECT_FALSE(om.has_options());
  EXPECT_NO_THROW(om.report());
}

TEST(option_map, get_bool_throws_when_present_with_value) {
  auto om = option_map("choice:flag=1");

  EXPECT_THAT([&] { om.get<bool>("flag"); },
              throws_runtime_error(
                  "value not allowed for option 'flag' of choice 'choice'"));
}

TEST(option_map, get_optional_returns_nullopt_when_absent) {
  auto om = option_map("choice");

  auto v = om.get_optional<int>("key");
  EXPECT_FALSE(v.has_value());

  EXPECT_FALSE(om.has_options());
  EXPECT_NO_THROW(om.report());
}

TEST(option_map, get_optional_returns_value_when_present_with_value) {
  auto om = option_map("choice:key=5");

  auto v = om.get_optional<int>("key");
  ASSERT_TRUE(v.has_value());
  EXPECT_EQ(*v, 5);

  EXPECT_FALSE(om.has_options());
  EXPECT_NO_THROW(om.report());
}

TEST(option_map, get_optional_throws_when_present_without_value) {
  auto om = option_map("choice:key");

  EXPECT_THAT([&] { om.get_optional<int>("key"); },
              throws_runtime_error(
                  "missing value for option 'key' of choice 'choice'"));
}

TEST(option_map,
     get_optional_string_returns_empty_string_for_empty_string_value) {
  auto om = option_map("choice:empty=");

  auto v = om.get_optional<std::string>("empty");
  ASSERT_TRUE(v.has_value());
  EXPECT_EQ(*v, "");

  EXPECT_FALSE(om.has_options());
  EXPECT_NO_THROW(om.report());
}

TEST(option_map, get_optional_with_default_returns_nullopt_when_absent) {
  auto om = option_map("choice");

  auto v = om.get_optional<int>("key", 11);
  EXPECT_FALSE(v.has_value());

  EXPECT_FALSE(om.has_options());
  EXPECT_NO_THROW(om.report());
}

TEST(option_map,
     get_optional_with_default_returns_default_when_present_without_value) {
  auto om = option_map("choice:key");

  auto v = om.get_optional<int>("key", 11);
  ASSERT_TRUE(v.has_value());
  EXPECT_EQ(*v, 11);

  EXPECT_FALSE(om.has_options());
  EXPECT_NO_THROW(om.report());
}

TEST(option_map,
     get_optional_with_default_returns_value_when_present_with_value) {
  auto om = option_map("choice:key=22");

  auto v = om.get_optional<int>("key", 11);
  ASSERT_TRUE(v.has_value());
  EXPECT_EQ(*v, 22);

  EXPECT_FALSE(om.has_options());
  EXPECT_NO_THROW(om.report());
}

TEST(option_map, get_size_returns_default_when_absent) {
  auto om = option_map("choice");

  EXPECT_EQ(om.get_size("size", 1234u), 1234u);

  EXPECT_FALSE(om.has_options());
  EXPECT_NO_THROW(om.report());
}

TEST(option_map, get_size_returns_value_when_present_with_value) {
  auto om = option_map("choice:size=123:size_with_unit=1m");

  EXPECT_EQ(om.get_size("size", 999u), 123u);
  EXPECT_EQ(om.get_size("size_with_unit", 999u), 1024u * 1024u);

  EXPECT_FALSE(om.has_options());
  EXPECT_NO_THROW(om.report());
}

TEST(option_map, get_size_throws_when_present_without_value) {
  auto om = option_map("choice:size");

  EXPECT_THAT([&] { om.get_size("size", 999u); },
              throws_runtime_error(
                  "missing value for option 'size' of choice 'choice'"));
}

TEST(option_map,
     getters_consume_options_and_report_succeeds_when_all_consumed) {
  auto om = option_map("choice:a=1:b=2");

  EXPECT_EQ(om.get<int>("a"), 1);
  EXPECT_TRUE(om.has_options()); // b still there

  EXPECT_EQ(om.get<int>("b"), 2);
  EXPECT_FALSE(om.has_options()); // all consumed

  EXPECT_NO_THROW(om.report());
}

TEST(option_map, report_throws_when_unconsumed_options_remain) {
  auto om = option_map("choice:a=1:b=2");

  om.get<int>("a");

  EXPECT_TRUE(om.has_options());
  EXPECT_THAT([&] { om.report(); },
              throws_runtime_error("invalid option(s) for choice 'choice': b"));
}

TEST(option_map, report_error_message_contains_sorted_invalid_keys) {
  auto om = option_map("choice:z=1:a=2");

  EXPECT_THAT(
      [&] { om.report(); },
      throws_runtime_error("invalid option(s) for choice 'choice': a, z"));
}

TEST(option_map, report_succeeds_when_no_options) {
  auto om = option_map("choice");
  EXPECT_NO_THROW(om.report());
}

TEST(option_map, accessing_unknown_key_does_not_consume_existing_options) {
  auto om = option_map("choice:present=1");

  EXPECT_THAT([&] { om.get<int>("missing"); },
              throws_runtime_error(
                  "missing value for option 'missing' of choice 'choice'"));

  EXPECT_TRUE(om.has_options());
  EXPECT_THAT(
      [&] { om.report(); },
      throws_runtime_error("invalid option(s) for choice 'choice': present"));
}

TEST(option_map,
     successful_get_consumes_key_and_report_mentions_only_remaining) {
  auto om = option_map("choice:a=1:b=2:c=3");

  EXPECT_EQ(om.get<int>("b"), 2);

  EXPECT_THAT(
      [&] { om.report(); },
      throws_runtime_error("invalid option(s) for choice 'choice': a, c"));
}

TEST(option_map, multiple_retrieval_of_required_value_dies_on_second_call) {
  auto om = option_map("choice:key=7");

  EXPECT_EQ(om.get<int>("key"), 7);

  EXPECT_DEATH(om.get<int>("key"),
               "option 'key' of choice 'choice' consumed multiple times");
}

TEST(option_map, multiple_retrieval_of_required_string_dies_on_second_call) {
  auto om = option_map("choice:name=alice");

  EXPECT_EQ(om.get<std::string>("name"), "alice");

  EXPECT_DEATH(om.get<std::string>("name"),
               "option 'name' of choice 'choice' consumed multiple times");
}

TEST(option_map, multiple_retrieval_of_optional_value_dies_on_second_call) {
  auto om = option_map("choice:key=7");

  auto first = om.get_optional<int>("key");
  ASSERT_TRUE(first.has_value());
  EXPECT_EQ(*first, 7);

  EXPECT_DEATH(om.get_optional<int>("key"),
               "option 'key' of choice 'choice' consumed multiple times");
}

TEST(option_map, multiple_retrieval_of_bool_dies_on_second_call) {
  auto om = option_map("choice:flag");

  EXPECT_TRUE(om.get<bool>("flag"));

  EXPECT_DEATH(om.get<bool>("flag"),
               "option 'flag' of choice 'choice' consumed multiple times");
}

TEST(option_map, empty_string_value_consumed_and_report_ok) {
  auto om = option_map("choice:empty=");

  EXPECT_EQ(om.get<std::string>("empty"), "");
  EXPECT_FALSE(om.has_options());
  EXPECT_NO_THROW(om.report());
}

TEST(option_map, empty_string_value_multiple_retrieval_dies_on_second_call) {
  auto om = option_map("choice:empty=");

  EXPECT_EQ(om.get<std::string>("empty"), "");

  EXPECT_DEATH(om.get<std::string>("empty"),
               "option 'empty' of choice 'choice' consumed multiple times");
}

TEST(option_map, report_does_not_mention_consumed_key) {
  auto om = option_map("choice:a=1:b=2:c=3");

  om.get<int>("b");

  EXPECT_THAT(
      [&] { om.report(); },
      throws_runtime_error("invalid option(s) for choice 'choice': a, c"));
}
