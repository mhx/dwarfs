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

#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <dwarfs/thrift_lite/compact_reader.h>
#include <dwarfs/thrift_lite/compact_writer.h>
#include <dwarfs/thrift_lite/debug_writer.h>
#include <dwarfs/thrift_lite/types.h>

#include <dwarfs/gen-cpp-lite/thrift_lite_test_types.h>

#include "thrift_lite_test_message.h"
#include "thrift_lite_test_message_data.h"

namespace {

using testing::ElementsAre;
using testing::ElementsAreArray;
using testing::HasSubstr;
using testing::Not;

namespace gen = dwarfs::thrift_lite::test;
namespace tl = dwarfs::thrift_lite;

auto serialize_compact(auto const& obj, bool const terse = true)
    -> std::vector<std::byte> {
  auto out = std::vector<std::byte>{};
  tl::writer_options opts;
  opts.terse = terse;
  auto w = tl::compact_writer{out, opts};
  obj.write(w);
  return out;
}

template <typename T>
auto deserialize_compact(std::vector<std::byte> const& bytes) -> T {
  auto r = tl::compact_reader{bytes};
  auto obj = T{};
  obj.read(r);
  return obj;
}

auto debug(auto const& msg) -> std::string {
  std::ostringstream out;
  auto w = tl::debug_writer{out};
  msg.write(w);
  return out.str();
};

} // namespace

static_assert(sizeof(gen::OnlyIntegral) == 16,
              "unexpected size for OnlyIntegral");

static_assert(std::is_same_v<gen::IntToStringMap,
                             std::unordered_map<std::int32_t, std::string>>,
              "unexpected type for IntToStringMap");

static_assert(
    std::is_same_v<
        std::remove_cvref_t<
            decltype(std::declval<gen::Containers>().id_to_name().value())>,
        std::map<std::int32_t, std::string>>,
    "unexpected type for Containers::id_to_name");

static_assert(
    std::is_same_v<
        std::remove_cvref_t<
            decltype(std::declval<gen::Containers>().opt_map().value())>,
        phmap::btree_map<std::int32_t, std::string>>,
    "unexpected type for Containers::opt_map");

static_assert(
    std::is_same_v<
        std::remove_cvref_t<
            decltype(std::declval<gen::Containers>().small_tags().value())>,
        std::set<std::string>>,
    "unexpected type for Containers::small_tags");

static_assert(
    std::is_same_v<
        std::remove_cvref_t<
            decltype(std::declval<gen::Containers>().opt_set().value())>,
        phmap::btree_set<std::int32_t>>,
    "unexpected type for Containers::opt_set");

TEST(thrift_lite_compact_roundtrip, terse_mode_everything_empty) {
  // Build a message where all fields are default / empty.
  auto msg = gen::TestMessage{};
  msg.header().value().version() = 0;
  msg.header().value().flags() = 0;

  auto const bytes = serialize_compact(msg);

  // expect only a ct_stop byte
  EXPECT_THAT(bytes, ElementsAre(std::byte{0}));

  auto const bytes_verbose = serialize_compact(msg, false);

  EXPECT_GT(bytes_verbose.size(), 50) << "verbose output unexpectedly small";

  auto msg_terse = deserialize_compact<gen::TestMessage>(bytes);
  auto msg_verbose = deserialize_compact<gen::TestMessage>(bytes_verbose);

  EXPECT_EQ(msg_terse, msg);
  EXPECT_EQ(msg_verbose, msg);
}

TEST(thrift_lite_compact_roundtrip, obj_msg_obj) {
  static constexpr unsigned num_msgs = 100;

  for (unsigned i = 0; i < num_msgs; ++i) {
    auto msg = dwarfs::test::make_random_thrift_lite_test_message(i);
    auto bytes = serialize_compact(msg);
    auto msg2 = deserialize_compact<gen::TestMessage>(bytes);
    EXPECT_EQ(msg, msg2) << "Roundtrip failed for message " << i;
    EXPECT_EQ(debug(msg), debug(msg2))
        << "Debug output differs for message " << i << ":\n---\n"
        << debug(msg) << "\n---\n"
        << debug(msg2) << "\n---";
  }
}

TEST(thrift_lite_compact_roundtrip, msg_obj_msg) {
  auto const messages = dwarfs::test::get_thrift_lite_test_messages();
  size_t i = 0;

  for (auto const& msg : messages) {
    auto const in = std::as_bytes(msg);

    auto r = tl::compact_reader{in};
    auto meta = gen::TestMessage{};
    meta.read(r);

    auto out = std::vector<std::byte>{};
    auto w = tl::compact_writer{out};
    meta.write(w);

    EXPECT_THAT(out, ElementsAreArray(in))
        << "Roundtrip failed for message " << i << " (byte output differs)";

    ++i;
  }
}

TEST(thrift_lite_compact_roundtrip, generate_test_data) {
  std::ostringstream oss;
  dwarfs::test::generate_thrift_lite_test_message_data(oss, 100);
  EXPECT_GT(oss.str().size(), 10'000)
      << "Test data generation failed" << oss.str();
}

class compact_roundtrip : public testing::TestWithParam<bool> {};

TEST_P(compact_roundtrip, compact_roundtrip_smoke_test_message) {
  auto const terse = GetParam();

  auto msg = gen::TestMessage{};

  // header (regular)
  msg.header().value().version() = 1;
  msg.header().value().flags() = gen::UInt16{7};

  // optional fields set
  msg.labels().emplace(gen::SmallStrings{});
  msg.labels().value().name() = "n";
  msg.labels().value().tag().emplace("t");

  // records (big, no optionals inside BigRecord)
  msg.records().value().emplace_back();
  msg.records().value().back().id() = 123;
  msg.records().value().back().kind() = gen::RecordKind::file;
  msg.records().value().back().checksum() = gen::UInt32{0x1234};
  msg.records().value().back().extents() = {1, 2, 3};

  // containers
  msg.containers().value().small_ints() = {1, 2};
  msg.containers().value().name_to_value().value().emplace("x", 7);

  // far field ids (exercise long-form headers / multi-byte ids)
  msg.far_optional().emplace(99);
  msg.far_regular() = 5;

  auto const bytes = serialize_compact(msg, terse);
  auto const msg2 = deserialize_compact<gen::TestMessage>(bytes);

  // Verify a handful of fields survived.
  EXPECT_EQ(msg2.header().value().version().value(), 1);
  EXPECT_EQ(static_cast<std::uint16_t>(msg2.header().value().flags().value()),
            7U);

  ASSERT_TRUE(msg2.labels().has_value());
  EXPECT_EQ(msg2.labels().value().name().value(), "n");
  ASSERT_TRUE(msg2.labels().value().tag().has_value());
  EXPECT_EQ(msg2.labels().value().tag().value(), "t");

  ASSERT_EQ(msg2.records().value().size(), 1U);
  EXPECT_EQ(msg2.records().value()[0].id().value(), 123);
  EXPECT_EQ(msg2.records().value()[0].kind().value(), gen::RecordKind::file);

  EXPECT_EQ(msg2.far_regular().value(), 5);
  ASSERT_TRUE(msg2.far_optional().has_value());
  EXPECT_EQ(msg2.far_optional().value(), 99);

  EXPECT_EQ(msg, msg2) << "Full roundtrip failed:\n---\n"
                       << debug(msg) << "\n---\n"
                       << debug(msg2) << "\n---";
}

TEST_P(compact_roundtrip, unknown_enum_values_are_accepted) {
  auto const terse = GetParam();

  // Serialize a BigRecord with kind set to an unknown numeric enum value by
  // mutating after parse.
  auto rec = gen::BigRecord{};
  // NOLINTNEXTLINE
  rec.kind() = static_cast<gen::RecordKind>(123456);
  rec.id() = 1;

  auto out = std::vector<std::byte>{};
  tl::writer_options opts;
  opts.terse = terse;
  auto w = tl::compact_writer{out, opts};
  rec.write(w);

  auto r = tl::compact_reader{out};
  auto rec2 = gen::BigRecord{};
  rec2.read(r);

  EXPECT_EQ(static_cast<int>(rec2.kind().value()), 123456);
}

TEST_P(compact_roundtrip, empty_optional_list_map_set) {
  auto const terse = GetParam();

  auto c = gen::Containers{};
  c.opt_list().emplace();
  c.opt_map().emplace();
  c.opt_set().emplace();

  auto out = std::vector<std::byte>{};
  tl::writer_options opts;
  opts.terse = terse;
  auto w = tl::compact_writer{out, opts};
  c.write(w);

  auto r = tl::compact_reader{out};
  auto c2 = gen::Containers{};
  c2.read(r);

  EXPECT_EQ(c2, c);
}

INSTANTIATE_TEST_SUITE_P(thrift_lite, compact_roundtrip, testing::Bool());
