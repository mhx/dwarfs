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

#include <limits>
#include <sstream>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <dwarfs/thrift_lite/compact_reader.h>
#include <dwarfs/thrift_lite/json_writer.h>

#include <dwarfs/gen-cpp-lite/thrift_lite_test_types.h>

#include "thrift_lite_test_message_data.h"

namespace {

using namespace std::string_view_literals;

namespace gen = dwarfs::thrift_lite::test;
namespace tl = dwarfs::thrift_lite;

gen::TestMessage get_test_message(size_t index) {
  auto msg = gen::TestMessage{};
  auto r = tl::compact_reader{
      std::as_bytes(dwarfs::test::get_thrift_lite_test_messages()[index])};
  msg.read(r);
  return msg;
}

} // namespace

TEST(thrift_lite, json_output_snapshot_for_small_object) {
  auto s = gen::SmallStrings{};
  s.name() = "alice";
  s.comment() = "hi";
  s.tag().emplace("t");

  auto oss = std::ostringstream{};
  auto opts = tl::json_writer_options{};
  opts.indent_step = 4;
  auto w = tl::json_writer{oss, opts};

  s.write(w);

  auto const got = oss.str();
  static constexpr auto expected = R"dbg({
    "name": "alice",
    "comment": "hi",
    "tag": "t"
})dbg"sv;

  EXPECT_TRUE(nlohmann::json::accept(got));
  EXPECT_EQ(expected, got);
}

TEST(thrift_lite, json_output_no_indent) {
  auto s = gen::SmallStrings{};
  s.name() = "alice";
  s.comment() = "hi";
  s.tag().emplace("t");

  auto oss = std::ostringstream{};
  auto opts = tl::json_writer_options{};
  opts.indent_step = 0;
  auto w = tl::json_writer{oss, opts};

  s.write(w);

  auto const got = oss.str();
  static constexpr auto expected =
      R"dbg({"name":"alice","comment":"hi","tag":"t"})dbg"sv;

  EXPECT_TRUE(nlohmann::json::accept(got));
  EXPECT_EQ(expected, got);
}

TEST(thrift_lite, json_output_everything_empty_terse) {
  auto msg = gen::Everything{};

  auto oss = std::ostringstream{};
  auto w = tl::json_writer{oss};
  msg.write(w);

  EXPECT_EQ("{}", oss.str());
}

TEST(thrift_lite, json_output_everything_empty_verbose) {
  auto msg = gen::Everything{};

  auto oss = std::ostringstream{};
  tl::json_writer_options opts;
  opts.terse = false;
  auto w = tl::json_writer{oss, opts};
  msg.write(w);

  static constexpr auto expected = R"dbg({
  "a_bool": false,
  "a_byte": 0,
  "a_int8": 0,
  "a_uint8": 0,
  "a_i16": 0,
  "a_int16": 0,
  "a_uint16": 0,
  "a_i32": 0,
  "a_int32": 0,
  "a_uint32": 0,
  "a_i64": 0,
  "a_int64": 0,
  "a_uint64": 0,
  "a_binary": "",
  "a_blob": "",
  "a_kind": 0,
  "a_string": "",
  "a_list": [],
  "a_set": [],
  "a_map": [],
  "a_struct": {
    "header": {
      "version": 0,
      "flags": 0
    },
    "records": [],
    "containers": {
      "small_ints": [],
      "small_tags": [],
      "id_to_name": [],
      "name_to_value": [],
      "nested_int_lists": [],
      "id_to_extents": []
    },
    "v1": {
      "a": 0,
      "c": 0,
      "containers": {
        "small_ints": [],
        "small_tags": [],
        "id_to_name": [],
        "name_to_value": [],
        "nested_int_lists": [],
        "id_to_extents": []
      }
    },
    "v2": {
      "a": 0,
      "c": 0,
      "containers": {
        "small_ints": [],
        "small_tags": [],
        "id_to_name": [],
        "name_to_value": [],
        "nested_int_lists": [],
        "id_to_extents": []
      }
    },
    "float_value": 0,
    "far_regular": 0
  }
})dbg"sv;

  auto const got = oss.str();

  EXPECT_TRUE(nlohmann::json::accept(got));
  EXPECT_EQ(expected, got);
}

TEST(thrift_lite, json_output_everything_full) {
  auto msg = gen::Everything{};

  // fill in all fields
  msg.a_bool() = true;
  msg.a_byte() = std::numeric_limits<std::int8_t>::min();
  msg.a_int8() = std::numeric_limits<std::int8_t>::max();
  msg.a_uint8() = std::numeric_limits<std::uint8_t>::max();
  msg.a_i16() = std::numeric_limits<std::int16_t>::min();
  msg.a_int16() = std::numeric_limits<std::int16_t>::max();
  msg.a_uint16() = std::numeric_limits<std::uint16_t>::max();
  msg.a_i32() = std::numeric_limits<std::int32_t>::min();
  msg.a_int32() = std::numeric_limits<std::int32_t>::max();
  msg.a_uint32() = std::numeric_limits<std::uint32_t>::max();
  msg.a_i64() = std::numeric_limits<std::int64_t>::min();
  msg.a_int64() = std::numeric_limits<std::int64_t>::max();
  msg.a_uint64() = std::numeric_limits<std::uint64_t>::max();
  msg.a_binary() = {std::byte{0x00}, std::byte{0x01}, std::byte{0x02},
                    std::byte{0x03}};
  msg.a_blob() = {std::byte{0x00}, std::byte{0x01}, std::byte{0x02},
                  std::byte{0x03}};
  msg.a_kind() = gen::RecordKind::directory;
  msg.a_string() = "hello";
  msg.a_list() = {1, 2, 3};
  msg.a_set() = {"a", "b", "c"};
  msg.a_map() = {{"x", gen::RecordKind::file},
                 {"y", gen::RecordKind::directory}};
  msg.a_struct() = get_test_message(0);
  msg.opt_bool() = false;
  msg.opt_kind() = gen::RecordKind::file;
  msg.opt_string().emplace();
  msg.opt_blob().emplace();
  msg.opt_list().emplace();
  msg.opt_set() = {4, 5, 6};
  msg.opt_map() = {{gen::RecordKind::symlink, "z"}};
  msg.opt_nested_map() = {{"k1", {"v1", "v2"}}};
  msg.opt_struct().emplace();

  auto oss = std::ostringstream{};
  auto w = tl::json_writer{oss};
  msg.write(w);

  static constexpr auto expected = R"dbg({
  "a_bool": true,
  "a_byte": -128,
  "a_int8": 127,
  "a_uint8": -1,
  "a_i16": -32768,
  "a_int16": 32767,
  "a_uint16": -1,
  "a_i32": -2147483648,
  "a_int32": 2147483647,
  "a_uint32": -1,
  "a_i64": -9223372036854775808,
  "a_int64": 9223372036854775807,
  "a_uint64": -1,
  "a_binary": "\u0000\u0001\u0002\u0003",
  "a_blob": "\u0000\u0001\u0002\u0003",
  "a_kind": 2,
  "a_string": "hello",
  "a_list": [
    1,
    2,
    3
  ],
  "a_set": [
    "a",
    "b",
    "c"
  ],
  "a_map": [
    ["x", 1],
    ["y", 2]
  ],
  "a_struct": {
    "v2": {
      "a": 1915067939,
      "c": 4405683035076197862,
      "f": {},
      "future_field": [
        [276867117, [
          {
            "name": "alqzyc",
            "comment": "ezqpgk",
            "tag": "qe",
            "payload": "\u00a8Q#\u00c72\u00f3^"
          },
          {
            "name": "vrczzw",
            "tag": "ltablh",
            "payload": "K{\u001e"
          },
          {},
          {}
        ]],
        [1788037497, [
          {},
          {
            "name": "ulnurdsq",
            "comment": "ytnck",
            "tag": "ut",
            "payload": "\"\u0004R\u009e&\u009c"
          },
          {
            "tag": "lxscbz"
          },
          {},
          {}
        ]]
      ]
    },
    "float_value": 1.5
  },
  "opt_bool": false,
  "opt_kind": 1,
  "opt_string": "",
  "opt_blob": "",
  "opt_list": [],
  "opt_set": [
    4,
    5,
    6
  ],
  "opt_map": [
    [3, "z"]
  ],
  "opt_nested_map": [
    ["k1", [
      "v1",
      "v2"
    ]]
  ],
  "opt_struct": {}
})dbg"sv;

  auto const got = oss.str();

  EXPECT_TRUE(nlohmann::json::accept(got));
  EXPECT_EQ(expected, got);
}
