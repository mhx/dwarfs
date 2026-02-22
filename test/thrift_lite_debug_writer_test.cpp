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

#include <dwarfs/thrift_lite/debug_writer.h>
#include <dwarfs/thrift_lite/types.h>

namespace {

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

TEST(debug_writer, writes_named_struct_and_fields) {
  auto oss = std::ostringstream{};
  auto w = tl::debug_writer{oss};

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
  auto const expected = R"dbg(Root{
  1: a (i32) = 42,
  2: b (bool) = true
})dbg";

  EXPECT_EQ(expected, got);
}

TEST(debug_writer, escapes_strings) {
  auto oss = std::ostringstream{};
  auto w = tl::debug_writer{oss};

  w.write_struct_begin("S");
  w.write_field_begin("s", tl::ttype::string_t, 1);
  w.write_string("a\"b\\c\n\r\t\x01");
  w.write_field_end();
  w.write_field_stop();
  w.write_struct_end();

  auto const got = oss.str();
  auto const expected = R"dbg(S{
  1: s (string) = "a\"b\\c\n\r\t\x01"
})dbg";

  EXPECT_EQ(expected, got);
}

TEST(debug_writer, writes_binary_with_hex_and_truncation) {
  auto oss = std::ostringstream{};
  auto w = tl::debug_writer{oss};

  auto const bytes = make_bytes(40);

  w.write_struct_begin("B");
  w.write_field_begin("bin", tl::ttype::binary_t, 1);
  w.write_binary(std::span<std::byte const>{bytes.data(), bytes.size()});
  w.write_field_end();
  w.write_field_stop();
  w.write_struct_end();

  auto const got = oss.str();
  auto const expected = R"dbg(B{
  1: bin (binary) = binary(len=40, hex=0x000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f...)
})dbg";

  EXPECT_EQ(expected, got);
}

TEST(debug_writer, writes_containers_and_nested_structs) {
  auto oss = std::ostringstream{};
  auto w = tl::debug_writer{oss};

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
  auto const expected = R"dbg(Outer{
  1: xs (list) = [
    1,
    2,
    300
  ],
  2: ys (set) = set{
    7,
    8
  },
  3: m (map) = map{
    1: "hi"
  },
  4: inner (struct) = Inner{
    1: z (byte) = 7
  }
})dbg";

  EXPECT_EQ(expected, got);
}

TEST(debug_writer, throws_on_write_struct_end_without_begin) {
  auto oss = std::ostringstream{};
  auto w = tl::debug_writer{oss};

  EXPECT_THAT([&] { w.write_struct_end(); },
              ThrowsMessage<tl::protocol_error>(HasSubstr(
                  "write_struct_end without matching write_struct_begin")));
}

TEST(debug_writer, throws_if_field_written_outside_struct) {
  auto oss = std::ostringstream{};
  auto w = tl::debug_writer{oss};

  EXPECT_THAT([&] { w.write_field_begin("a", tl::ttype::i32_t, 1); },
              ThrowsMessage<tl::protocol_error>(
                  HasSubstr("fields can only be written inside a struct")));
}

TEST(debug_writer, throws_if_field_end_without_value) {
  auto oss = std::ostringstream{};
  auto w = tl::debug_writer{oss};

  w.write_struct_begin("S");
  w.write_field_begin("a", tl::ttype::i32_t, 1);

  EXPECT_THAT([&] { w.write_field_end(); },
              ThrowsMessage<tl::protocol_error>(
                  HasSubstr("write_field_end without a value")));
}

TEST(debug_writer, throws_on_negative_container_sizes) {
  auto oss = std::ostringstream{};
  auto w = tl::debug_writer{oss};

  w.write_struct_begin("S");

  w.write_field_begin("l", tl::ttype::list_t, 1);
  EXPECT_THAT(
      [&] { w.write_list_begin(tl::ttype::i32_t, -1); },
      ThrowsMessage<tl::protocol_error>(HasSubstr("negative list size")));

  w.write_field_begin("s", tl::ttype::set_t, 2);
  EXPECT_THAT(
      [&] { w.write_set_begin(tl::ttype::i32_t, -1); },
      ThrowsMessage<tl::protocol_error>(HasSubstr("negative set size")));

  w.write_field_begin("m", tl::ttype::map_t, 3);
  EXPECT_THAT(
      [&] { w.write_map_begin(tl::ttype::i32_t, tl::ttype::i32_t, -1); },
      ThrowsMessage<tl::protocol_error>(HasSubstr("negative map size")));
}

TEST(debug_writer, list_end_throws_if_not_all_elements_written) {
  auto oss = std::ostringstream{};
  auto w = tl::debug_writer{oss};

  w.write_struct_begin("S");
  w.write_field_begin("l", tl::ttype::list_t, 1);

  w.write_list_begin(tl::ttype::i32_t, 2);
  w.write_i32(1);

  EXPECT_THAT([&] { w.write_list_end(); },
              ThrowsMessage<tl::protocol_error>(
                  HasSubstr("not all list elements were written")));
}

TEST(debug_writer, throws_if_too_many_list_elements_written) {
  auto oss = std::ostringstream{};
  auto w = tl::debug_writer{oss};

  w.write_struct_begin("S");
  w.write_field_begin("l", tl::ttype::list_t, 1);

  w.write_list_begin(tl::ttype::i32_t, 1);
  w.write_i32(1);

  EXPECT_THAT([&] { w.write_i32(2); },
              ThrowsMessage<tl::protocol_error>(
                  HasSubstr("too many container elements written")));
}

TEST(debug_writer, map_end_throws_if_expecting_value) {
  auto oss = std::ostringstream{};
  auto w = tl::debug_writer{oss};

  w.write_struct_begin("S");
  w.write_field_begin("m", tl::ttype::map_t, 1);

  w.write_map_begin(tl::ttype::i32_t, tl::ttype::i32_t, 1);
  w.write_i32(1); // key only, no value

  EXPECT_THAT([&] { w.write_map_end(); },
              ThrowsMessage<tl::protocol_error>(
                  HasSubstr("map ended while expecting a value")));
}

TEST(debug_writer, throws_if_too_many_map_entries_written) {
  auto oss = std::ostringstream{};
  auto w = tl::debug_writer{oss};

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

TEST(debug_writer, write_double_is_not_supported) {
  auto oss = std::ostringstream{};
  auto w = tl::debug_writer{oss};

  EXPECT_THAT([&] { w.write_double(1.0); },
              ThrowsMessage<tl::protocol_error>(HasSubstr(
                  "double type not supported in this implementation")));
}
