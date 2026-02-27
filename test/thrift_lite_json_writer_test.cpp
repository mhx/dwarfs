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
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <dwarfs/thrift_lite/json_writer.h>
#include <dwarfs/thrift_lite/types.h>

namespace {

using namespace std::string_view_literals;

using testing::HasSubstr;
using testing::Not;
using testing::StartsWith;
using testing::ThrowsMessage;

namespace tl = dwarfs::thrift_lite;

auto make_bytes(std::size_t const n) -> std::vector<std::byte> {
  std::vector<std::byte> out;
  out.reserve(n);
  for (std::size_t i = 0; i < n; ++i) {
    out.push_back(static_cast<std::byte>(static_cast<std::uint8_t>(i & 0xff)));
  }
  return out;
}

} // namespace

TEST(json_writer, writes_named_struct_and_fields) {
  auto oss = std::ostringstream{};
  auto w = tl::json_writer{oss};

  w.write_struct_begin("Root");

  w.write_field_begin("a", tl::ttype::i32_t, 1);
  w.write_i32(42);
  w.write_field_end();

  w.write_field_begin("b", tl::ttype::bool_t, 2);
  w.write_bool(true);
  w.write_field_end();

  w.write_field_stop();
  w.write_struct_end();

  auto const got = oss.str();
  auto const expected = R"dbg({
  "a": 42,
  "b": true
})dbg";

  EXPECT_EQ(expected, got);
}

TEST(json_writer, escapes_strings) {
  auto oss = std::ostringstream{};
  auto w = tl::json_writer{oss};

  w.write_struct_begin("S");
  w.write_field_begin("s", tl::ttype::string_t, 1);
  w.write_string("a\"b\\c\n\r\t\x01");
  w.write_field_end();
  w.write_field_stop();
  w.write_struct_end();

  auto const got = oss.str();
  auto const expected = R"dbg({
  "s": "a\"b\\c\n\r\t\u0001"
})dbg";

  EXPECT_EQ(expected, got);
}

TEST(json_writer, writes_binary_as_string) {
  auto oss = std::ostringstream{};
  auto w = tl::json_writer{oss};

  auto const bytes = make_bytes(40);

  w.write_struct_begin("B");
  w.write_field_begin("bin", tl::ttype::binary_t, 1);
  w.write_binary(std::span<std::byte const>{bytes.data(), bytes.size()});
  w.write_field_end();
  w.write_field_stop();
  w.write_struct_end();

  auto const got = oss.str();
  auto const expected = R"dbg({
  "bin": "\u0000\u0001\u0002\u0003\u0004\u0005\u0006\u0007\b\t\n\u000b\f\r\u000e\u000f\u0010\u0011\u0012\u0013\u0014\u0015\u0016\u0017\u0018\u0019\u001a\u001b\u001c\u001d\u001e\u001f !\"#$%&'"
})dbg";

  EXPECT_EQ(expected, got);
}

TEST(json_writer, writes_containers_and_nested_structs) {
  auto oss = std::ostringstream{};
  auto w = tl::json_writer{oss};

  w.write_struct_begin("Outer");

  w.write_field_begin("xs", tl::ttype::list_t, 1);
  w.write_list_begin(tl::ttype::i16_t, 3);
  w.write_i16(1);
  w.write_i16(2);
  w.write_i16(300);
  w.write_list_end();
  w.write_field_end();

  w.write_field_begin("ys", tl::ttype::set_t, 2);
  w.write_set_begin(tl::ttype::i32_t, 2);
  w.write_i32(7);
  w.write_i32(8);
  w.write_set_end();
  w.write_field_end();

  w.write_field_begin("m", tl::ttype::map_t, 3);
  w.write_map_begin(tl::ttype::i32_t, tl::ttype::string_t, 1);
  w.write_i32(1);
  w.write_string("hi");
  w.write_map_end();
  w.write_field_end();

  w.write_field_begin("inner", tl::ttype::struct_t, 4);
  w.write_struct_begin("Inner");
  w.write_field_begin("z", tl::ttype::byte_t, 1);
  w.write_byte(7);
  w.write_field_end();
  w.write_field_stop();
  w.write_struct_end();
  w.write_field_end();

  w.write_field_stop();
  w.write_struct_end();

  auto const got = oss.str();
  auto const expected = R"dbg({
  "xs": [
    1,
    2,
    300
  ],
  "ys": [
    7,
    8
  ],
  "m": [
    [1, "hi"]
  ],
  "inner": {
    "z": 7
  }
})dbg";

  EXPECT_EQ(expected, got);
}

TEST(json_writer, throws_on_write_struct_end_without_begin) {
  auto oss = std::ostringstream{};
  auto w = tl::json_writer{oss};

  EXPECT_THAT([&] { w.write_struct_end(); },
              ThrowsMessage<tl::protocol_error>(HasSubstr(
                  "write_struct_end without matching write_struct_begin")));
}

TEST(json_writer, throws_if_field_written_outside_struct) {
  auto oss = std::ostringstream{};
  auto w = tl::json_writer{oss};

  EXPECT_THAT([&] { w.write_field_begin("a", tl::ttype::i32_t, 1); },
              ThrowsMessage<tl::protocol_error>(
                  HasSubstr("fields can only be written inside a struct")));
}

TEST(json_writer, throws_if_field_end_without_value) {
  auto oss = std::ostringstream{};
  auto w = tl::json_writer{oss};

  w.write_struct_begin("S");
  w.write_field_begin("a", tl::ttype::i32_t, 1);

  EXPECT_THAT([&] { w.write_field_end(); },
              ThrowsMessage<tl::protocol_error>(
                  HasSubstr("write_field_end without a value")));
}

TEST(json_writer, list_end_throws_if_not_all_elements_written) {
  auto oss = std::ostringstream{};
  auto w = tl::json_writer{oss};

  w.write_struct_begin("S");
  w.write_field_begin("l", tl::ttype::list_t, 1);

  w.write_list_begin(tl::ttype::i32_t, 2);
  w.write_i32(1);

  EXPECT_THAT([&] { w.write_list_end(); },
              ThrowsMessage<tl::protocol_error>(
                  HasSubstr("not all list elements were written")));
}

TEST(json_writer, set_end_throws_if_not_all_elements_written) {
  auto oss = std::ostringstream{};
  auto w = tl::json_writer{oss};

  w.write_struct_begin("S");
  w.write_field_begin("s", tl::ttype::set_t, 1);

  w.write_set_begin(tl::ttype::i32_t, 2);
  w.write_i32(1);

  EXPECT_THAT([&] { w.write_set_end(); },
              ThrowsMessage<tl::protocol_error>(
                  HasSubstr("not all set elements were written")));
}

TEST(json_writer, map_end_throws_if_not_all_entries_written) {
  auto oss = std::ostringstream{};
  auto w = tl::json_writer{oss};

  w.write_struct_begin("S");
  w.write_field_begin("m", tl::ttype::map_t, 1);

  w.write_map_begin(tl::ttype::i32_t, tl::ttype::i32_t, 2);
  w.write_i32(1);
  w.write_i32(2); // completes the first entry

  EXPECT_THAT([&] { w.write_map_end(); },
              ThrowsMessage<tl::protocol_error>(
                  HasSubstr("not all map entries were written")));
}

TEST(json_writer, throws_if_too_many_list_elements_written) {
  auto oss = std::ostringstream{};
  auto w = tl::json_writer{oss};

  w.write_struct_begin("S");
  w.write_field_begin("l", tl::ttype::list_t, 1);

  w.write_list_begin(tl::ttype::i32_t, 1);
  w.write_i32(1);

  EXPECT_THAT([&] { w.write_i32(2); },
              ThrowsMessage<tl::protocol_error>(
                  HasSubstr("too many container elements written")));
}

TEST(json_writer, map_end_throws_if_expecting_value) {
  auto oss = std::ostringstream{};
  auto w = tl::json_writer{oss};

  w.write_struct_begin("S");
  w.write_field_begin("m", tl::ttype::map_t, 1);

  w.write_map_begin(tl::ttype::i32_t, tl::ttype::i32_t, 1);
  w.write_i32(1); // key only, no value

  EXPECT_THAT([&] { w.write_map_end(); },
              ThrowsMessage<tl::protocol_error>(
                  HasSubstr("map ended while expecting a value")));
}

TEST(json_writer, throws_if_too_many_map_entries_written) {
  auto oss = std::ostringstream{};
  auto w = tl::json_writer{oss};

  w.write_struct_begin("S");
  w.write_field_begin("m", tl::ttype::map_t, 1);

  w.write_map_begin(tl::ttype::i32_t, tl::ttype::i32_t, 1);
  w.write_i32(1);
  w.write_i32(2); // completes the only entry

  EXPECT_THAT(
      [&] {
        w.write_i32(3); // start 2nd key -> should fail
      },
      ThrowsMessage<tl::protocol_error>(
          HasSubstr("too many map entries written")));
}

TEST(json_writer, write_double_preserves_precision) {
  auto oss = std::ostringstream{};
  auto w = tl::json_writer{oss};

  w.write_double(1.234567890123456);

  auto const got = oss.str();
  static constexpr auto expected = "1.234567890123456"sv;

  EXPECT_EQ(expected, got);
}

TEST(json_writer, write_double_uses_scientific_notation) {
  auto oss = std::ostringstream{};
  auto w = tl::json_writer{oss};

  w.write_double(1.234567890123456e+300);

  auto const got = oss.str();
  static constexpr auto expected = "1.234567890123456e+300"sv;

  EXPECT_EQ(expected, got);
}

TEST(json_writer, write_double_preserves_special_values) {
  auto oss = std::ostringstream{};
  auto w = tl::json_writer{oss};

  w.write_struct_begin("S");
  w.write_field_begin("a", tl::ttype::double_t, 1);
  w.write_double(std::numeric_limits<double>::quiet_NaN());
  w.write_field_end();
  w.write_field_begin("b", tl::ttype::double_t, 2);
  w.write_double(std::numeric_limits<double>::infinity());
  w.write_field_end();
  w.write_field_begin("c", tl::ttype::double_t, 3);
  w.write_double(-std::numeric_limits<double>::infinity());
  w.write_field_end();
  w.write_field_stop();
  w.write_struct_end();

  auto const got = oss.str();
  static constexpr auto expected = R"dbg({
  "a": "NaN",
  "b": "Infinity",
  "c": "-Infinity"
})dbg"sv;

  EXPECT_EQ(expected, got);
}

TEST(json_writer, logic_errors) {
  auto oss = std::ostringstream{};
  auto w = std::optional<tl::json_writer>{};
  w.emplace(oss);
  EXPECT_THAT([&] { w->write_field_end(); },
              ThrowsMessage<tl::protocol_error>(
                  HasSubstr("write_field_end outside of a struct")));
  w->write_struct_begin("TestStruct");
  EXPECT_THAT([&] { w->write_list_end(); },
              ThrowsMessage<tl::protocol_error>(HasSubstr(
                  "write_list_end without matching write_list_begin")));
  EXPECT_THAT([&] { w->write_set_end(); },
              ThrowsMessage<tl::protocol_error>(
                  HasSubstr("write_set_end without matching write_set_begin")));
  EXPECT_THAT([&] { w->write_map_end(); },
              ThrowsMessage<tl::protocol_error>(
                  HasSubstr("write_map_end without matching write_map_begin")));
  w->write_field_begin("a_list", tl::ttype::list_t, 1);
  EXPECT_THAT([&] { w->write_field_begin("oops", tl::ttype::i32_t, 2); },
              ThrowsMessage<tl::protocol_error>(
                  HasSubstr("write_field_begin while expecting a value")));
  EXPECT_THAT([&] { w->write_struct_end(); },
              ThrowsMessage<tl::protocol_error>(
                  HasSubstr("struct ended while expecting a value")));
}
