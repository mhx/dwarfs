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

#include <dwarfs/thrift_lite/compact_reader.h>
#include <dwarfs/thrift_lite/debug_writer.h>

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

TEST(thrift_lite, debug_output_snapshot_for_small_object) {
  auto s = gen::SmallStrings{};
  s.name() = "alice";
  s.comment() = "hi";
  s.tag().emplace("t");

  auto oss = std::ostringstream{};
  auto w = tl::debug_writer{oss};

  s.write(w);

  auto const got = oss.str();
  static constexpr auto expected = R"dbg(SmallStrings{
  1: name (string) = "alice",
  2: comment (string) = "hi",
  3: tag (string) = "t"
})dbg"sv;

  EXPECT_EQ(expected, got);
}

TEST(thrift_lite, debug_output_everything_empty_terse) {
  auto msg = gen::Everything{};

  auto oss = std::ostringstream{};
  auto w = tl::debug_writer{oss};
  msg.write(w);

  EXPECT_EQ("Everything{}"sv, oss.str());
}

TEST(thrift_lite, debug_output_everything_empty_verbose) {
  auto msg = gen::Everything{};

  auto oss = std::ostringstream{};
  auto w = tl::debug_writer{oss, {.terse = false}};
  msg.write(w);

  static constexpr auto expected = R"dbg(Everything{
  1: a_bool (bool) = false,
  2: a_byte (byte) = 0,
  3: a_int8 (byte) = 0,
  4: a_uint8 (byte) = 0,
  5: a_i16 (i16) = 0,
  6: a_int16 (i16) = 0,
  7: a_uint16 (i16) = 0,
  8: a_i32 (i32) = 0,
  9: a_int32 (i32) = 0,
  10: a_uint32 (i32) = 0,
  11: a_i64 (i64) = 0,
  12: a_int64 (i64) = 0,
  13: a_uint64 (i64) = 0,
  14: a_binary (binary) = binary(len=0, hex=0x),
  15: a_blob (binary) = binary(len=0, hex=0x),
  16: a_kind (i32) = 0,
  17: a_string (string) = "",
  18: a_list (list) = [],
  19: a_set (set) = set{},
  20: a_map (map) = map{},
  21: a_struct (struct) = TestMessage{
    1: header (struct) = Header{
      1: version (i32) = 0,
      4: flags (i16) = 0
    },
    3: records (list) = [],
    4: containers (struct) = Containers{
      1: small_ints (list) = [],
      2: small_tags (set) = set{},
      3: id_to_name (map) = map{},
      4: name_to_value (map) = map{},
      5: nested_int_lists (list) = [],
      6: id_to_extents (map) = map{}
    },
    5: v1 (struct) = CompatV1{
      1: a (i32) = 0,
      3: c (i64) = 0,
      5: containers (struct) = Containers{
        1: small_ints (list) = [],
        2: small_tags (set) = set{},
        3: id_to_name (map) = map{},
        4: name_to_value (map) = map{},
        5: nested_int_lists (list) = [],
        6: id_to_extents (map) = map{}
      }
    },
    6: v2 (struct) = CompatV2{
      1: a (i32) = 0,
      3: c (i64) = 0,
      5: containers (struct) = Containers{
        1: small_ints (list) = [],
        2: small_tags (set) = set{},
        3: id_to_name (map) = map{},
        4: name_to_value (map) = map{},
        5: nested_int_lists (list) = [],
        6: id_to_extents (map) = map{}
      }
    },
    300: far_regular (i32) = 0
  }
})dbg"sv;

  EXPECT_EQ(expected, oss.str());
}

TEST(thrift_lite, debug_output_everything_full) {
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
  auto w = tl::debug_writer{oss};
  msg.write(w);

  static constexpr auto expected = R"dbg(Everything{
  1: a_bool (bool) = true,
  2: a_byte (byte) = -128,
  3: a_int8 (byte) = 127,
  4: a_uint8 (byte) = -1,
  5: a_i16 (i16) = -32768,
  6: a_int16 (i16) = 32767,
  7: a_uint16 (i16) = -1,
  8: a_i32 (i32) = -2147483648,
  9: a_int32 (i32) = 2147483647,
  10: a_uint32 (i32) = -1,
  11: a_i64 (i64) = -9223372036854775808,
  12: a_int64 (i64) = 9223372036854775807,
  13: a_uint64 (i64) = -1,
  14: a_binary (binary) = binary(len=4, hex=0x00010203),
  15: a_blob (binary) = binary(len=4, hex=0x00010203),
  16: a_kind (i32) = 2,
  17: a_string (string) = "hello",
  18: a_list (list) = [
    1,
    2,
    3
  ],
  19: a_set (set) = set{
    "a",
    "b",
    "c"
  },
  20: a_map (map) = map{
    "x": 1,
    "y": 2
  },
  21: a_struct (struct) = TestMessage{
    6: v2 (struct) = CompatV2{
      1: a (i32) = 1915067939,
      3: c (i64) = 4405683035076197862,
      7: f (struct) = SmallStrings{},
      50: future_field (map) = map{
        276867117: [
          SmallStrings{
            1: name (string) = "alqzyc",
            2: comment (string) = "ezqpgk",
            3: tag (string) = "qe",
            4: payload (binary) = binary(len=7, hex=0xa85123c732f35e)
          },
          SmallStrings{
            1: name (string) = "vrczzw",
            3: tag (string) = "ltablh",
            4: payload (binary) = binary(len=3, hex=0x4b7b1e)
          },
          SmallStrings{},
          SmallStrings{}
        ],
        1788037497: [
          SmallStrings{},
          SmallStrings{
            1: name (string) = "ulnurdsq",
            2: comment (string) = "ytnck",
            3: tag (string) = "ut",
            4: payload (binary) = binary(len=6, hex=0x2204529e269c)
          },
          SmallStrings{
            3: tag (string) = "lxscbz"
          },
          SmallStrings{},
          SmallStrings{}
        ]
      }
    },
    100: far_optional (i32) = 465621985
  },
  22: opt_bool (bool) = false,
  23: opt_kind (i32) = 1,
  24: opt_string (string) = "",
  25: opt_blob (binary) = binary(len=0, hex=0x),
  26: opt_list (list) = [],
  27: opt_set (set) = set{
    4,
    5,
    6
  },
  28: opt_map (map) = map{
    3: "z"
  },
  29: opt_nested_map (map) = map{
    "k1": [
      "v1",
      "v2"
    ]
  },
  30: opt_struct (struct) = TestMessage{}
})dbg"sv;

  EXPECT_EQ(expected, oss.str());
}
