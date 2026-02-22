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
#include <initializer_list>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <dwarfs/thrift_lite/compact_writer.h>
#include <dwarfs/thrift_lite/types.h>

namespace {

using testing::ElementsAre;
using testing::HasSubstr;
using testing::ThrowsMessage;

namespace tl = dwarfs::thrift_lite;

// Compact protocol type ids (low nibble), per spec.
constexpr inline std::uint8_t ct_stop [[maybe_unused]] = 0x00;
constexpr inline std::uint8_t ct_boolean_true [[maybe_unused]] = 0x01;
constexpr inline std::uint8_t ct_boolean_false [[maybe_unused]] = 0x02;
constexpr inline std::uint8_t ct_byte [[maybe_unused]] = 0x03;
constexpr inline std::uint8_t ct_i16 [[maybe_unused]] = 0x04;
constexpr inline std::uint8_t ct_i32 [[maybe_unused]] = 0x05;
constexpr inline std::uint8_t ct_i64 [[maybe_unused]] = 0x06;
constexpr inline std::uint8_t ct_binary [[maybe_unused]] = 0x08;
constexpr inline std::uint8_t ct_list [[maybe_unused]] = 0x09;
constexpr inline std::uint8_t ct_set [[maybe_unused]] = 0x0a;
constexpr inline std::uint8_t ct_map [[maybe_unused]] = 0x0b;
constexpr inline std::uint8_t ct_struct [[maybe_unused]] = 0x0c;

auto as_u8s(std::vector<std::byte> const& bytes) -> std::vector<std::uint8_t> {
  std::vector<std::uint8_t> out;
  out.reserve(bytes.size());
  for (auto const b : bytes) {
    out.push_back(static_cast<std::uint8_t>(b));
  }
  return out;
}

} // namespace

TEST(compact_writer, throws_on_unbalanced_struct_end) {
  std::vector<std::byte> out;
  auto w = tl::compact_writer{out};

  EXPECT_THAT([&] { w.write_struct_end(); },
              ThrowsMessage<tl::protocol_error>(HasSubstr(
                  "write_struct_end without matching write_struct_begin")));
}

TEST(compact_writer, write_double_is_not_supported) {
  std::vector<std::byte> out;
  auto w = tl::compact_writer{out};

  EXPECT_THAT([&] { w.write_double(1.0); },
              ThrowsMessage<tl::protocol_error>(HasSubstr(
                  "double type not supported in this implementation")));
}

TEST(compact_writer,
     encodes_bool_field_value_in_header_and_requires_immediate_write_bool) {
  std::vector<std::byte> out;
  auto w = tl::compact_writer{out};

  w.write_struct_begin({});

  w.write_field_begin({}, tl::ttype::bool_t, 1);
  w.write_bool(true);
  w.write_field_end();

  w.write_field_begin({}, tl::ttype::bool_t, 2);
  w.write_bool(false);
  w.write_field_end();

  w.write_field_stop();
  w.write_struct_end();

  // field 1 true: delta=1 header = (1<<4)|TRUE = 0x11
  // field 2 false: delta=1 header = (1<<4)|FALSE = 0x12
  EXPECT_THAT(as_u8s(out), ElementsAre(0x11, 0x12, ct_stop));
}

TEST(compact_writer,
     throws_if_bool_field_begin_not_followed_by_write_bool_before_next_action) {
  std::vector<std::byte> out;
  auto w = tl::compact_writer{out};

  w.write_struct_begin({});
  w.write_field_begin({}, tl::ttype::bool_t, 1);

  EXPECT_THAT(
      [&] { w.write_field_begin({}, tl::ttype::i32_t, 2); },
      ThrowsMessage<tl::protocol_error>(HasSubstr("bool field pending")));

  EXPECT_THAT(
      [&] { w.write_field_stop(); },
      ThrowsMessage<tl::protocol_error>(HasSubstr("bool field pending")));

  EXPECT_THAT(
      [&] { w.write_struct_end(); },
      ThrowsMessage<tl::protocol_error>(HasSubstr("bool field pending")));
}

TEST(compact_writer,
     writes_many_field_types_and_containers_exercising_type_mapping) {
  std::vector<std::byte> out;
  auto w = tl::compact_writer{out};

  w.write_struct_begin({});

  // 1) byte
  w.write_field_begin({}, tl::ttype::byte_t, 1);
  w.write_byte(-1);
  w.write_field_end();

  // 2) i16 with multi-byte varint (zigzag(300)=600 => 0xD8 0x04)
  w.write_field_begin({}, tl::ttype::i16_t, 2);
  w.write_i16(300);
  w.write_field_end();

  // 3) i32 with multi-byte varint (zigzag(1000)=2000 => 0xD0 0x0F)
  w.write_field_begin({}, tl::ttype::i32_t, 3);
  w.write_i32(1000);
  w.write_field_end();

  // 4) i64 with multi-byte varint via negative number (zigzag(-1000)=1999 =>
  // 0xCF 0x0F)
  w.write_field_begin({}, tl::ttype::i64_t, 4);
  w.write_i64(-1000);
  w.write_field_end();

  // 5) string (alias of binary)
  w.write_field_begin({}, tl::ttype::string_t, 5);
  w.write_string("hi");
  w.write_field_end();

  // 6) nested struct field
  w.write_field_begin({}, tl::ttype::struct_t, 6);
  w.write_struct_begin({});
  w.write_field_begin({}, tl::ttype::byte_t, 1);
  w.write_byte(7);
  w.write_field_end();
  w.write_field_stop();
  w.write_struct_end();
  w.write_field_end();

  // 7) list<bool> (size=3)
  w.write_field_begin({}, tl::ttype::list_t, 7);
  w.write_list_begin(tl::ttype::bool_t, 3);
  w.write_bool(true);
  w.write_bool(false);
  w.write_bool(true);
  w.write_list_end();
  w.write_field_end();

  // 8) set<i16> (size=1)
  w.write_field_begin({}, tl::ttype::set_t, 8);
  w.write_set_begin(tl::ttype::i16_t, 1);
  w.write_i16(1);
  w.write_set_end();
  w.write_field_end();

  // 9) empty map<i32,i32>
  w.write_field_begin({}, tl::ttype::map_t, 9);
  w.write_map_begin(tl::ttype::i32_t, tl::ttype::i32_t, 0);
  w.write_map_end();
  w.write_field_end();

  w.write_field_stop();
  w.write_struct_end();

  // Expected bytes:
  //  field1: (1<<4)|ct_byte = 0x13, byte(-1)=0xFF
  //  field2: 0x14, i16(300): 0xD8 0x04
  //  field3: 0x15, i32(1000): 0xD0 0x0F
  //  field4: 0x16, i64(-1000): 0xCF 0x0F
  //  field5: (1<<4)|ct_binary=0x18, len=2, 'h','i'
  //  field6: (1<<4)|ct_struct=0x1C, then nested struct: 0x13, 0x07, stop
  //  field7: (1<<4)|ct_list=0x19, list hdr (3<<4)|TRUE=0x31, elems 0x01 0x02
  //  0x01 field8: (1<<4)|ct_set=0x1A, set hdr (1<<4)|ct_i16=0x14, elem
  //  i16(1)=zigzag(1)=2 => 0x02 field9: (1<<4)|ct_map=0x1B, empty map marker
  //  0x00 stop: 0x00
  EXPECT_THAT(
      as_u8s(out),
      ElementsAre(0x13, 0xff, 0x14, 0xd8, 0x04, 0x15, 0xd0, 0x0f, 0x16, 0xcf,
                  0x0f, 0x18, 0x02, static_cast<std::uint8_t>('h'),
                  static_cast<std::uint8_t>('i'), 0x1c, 0x13, 0x07, ct_stop,
                  0x19, 0x31, ct_boolean_true, ct_boolean_false,
                  ct_boolean_true, 0x1a, 0x14, 0x02, 0x1b, 0x00, ct_stop));
}

TEST(compact_writer, encodes_list_long_form_when_size_exceeds_14) {
  std::vector<std::byte> out;
  auto w = tl::compact_writer{out};

  w.write_struct_begin({});

  w.write_field_begin({}, tl::ttype::list_t, 1);
  w.write_list_begin(tl::ttype::i32_t, 15);
  for (auto i = 0; i < 15; ++i) {
    w.write_i32(i);
  }
  w.write_list_end();
  w.write_field_end();

  w.write_field_stop();
  w.write_struct_end();

  // field header: (1<<4)|ct_list = 0x19
  // list long header: 0xF0|ct_i32 = 0xF5, size varint(15)=0x0F
  // elems: i32(i) => zigzag(i)=2*i, all single-byte varints for i in [0..14]
  // stop
  EXPECT_THAT(as_u8s(out), ElementsAre(0x19, 0xf5, 0x0f, 0x00, 0x02, 0x04, 0x06,
                                       0x08, 0x0a, 0x0c, 0x0e, 0x10, 0x12, 0x14,
                                       0x16, 0x18, 0x1a, 0x1c, ct_stop));
}

TEST(compact_writer, writes_binary_length_with_multi_byte_varint) {
  std::vector<std::byte> out;
  auto w = tl::compact_writer{out};

  // Length 200 => varint bytes: 0xC8 0x01
  std::string payload(200, 'a');
  w.write_string(payload);

  auto const u8s = as_u8s(out);
  ASSERT_GE(u8s.size(), 2U);
  EXPECT_EQ(u8s[0], 0xc8);
  EXPECT_EQ(u8s[1], 0x01);
  EXPECT_EQ(u8s.size(), 2U + payload.size());
  EXPECT_EQ(u8s[2], static_cast<std::uint8_t>('a'));
  EXPECT_EQ(u8s.back(), static_cast<std::uint8_t>('a'));
}

TEST(compact_writer,
     field_id_stack_restores_last_field_id_across_nested_struct_scopes) {
  std::vector<std::byte> out;
  auto w = tl::compact_writer{out};

  w.write_struct_begin({});

  w.write_field_begin({}, tl::ttype::i32_t, 10);
  w.write_i32(0);
  w.write_field_end();

  w.write_field_begin({}, tl::ttype::struct_t, 11);
  w.write_struct_begin({});
  w.write_field_begin({}, tl::ttype::i32_t, 1);
  w.write_i32(0);
  w.write_field_end();
  w.write_field_stop();
  w.write_struct_end();
  w.write_field_end();

  w.write_field_begin({}, tl::ttype::i32_t, 12);
  w.write_i32(0);
  w.write_field_end();

  w.write_field_stop();
  w.write_struct_end();

  // field 10: delta=10 => (10<<4)|ct_i32 = 0xA5, value 0x00
  // field 11: delta=1  => (1<<4)|ct_struct = 0x1C, then nested field 1 => 0x15
  // 0x00, stop field 12: after nested struct_end restores last_field_id=11,
  // delta=1 => 0x15 0x00 stop
  EXPECT_THAT(as_u8s(out), ElementsAre(0xa5, 0x00, 0x1c, 0x15, 0x00, ct_stop,
                                       0x15, 0x00, ct_stop));
}

TEST(compact_writer, throws_on_negative_container_sizes) {
  std::vector<std::byte> out;
  auto w = tl::compact_writer{out};

  EXPECT_THAT(
      [&] { w.write_list_begin(tl::ttype::i32_t, -1); },
      ThrowsMessage<tl::protocol_error>(HasSubstr("negative list size")));

  EXPECT_THAT(
      [&] { w.write_set_begin(tl::ttype::i32_t, -1); },
      ThrowsMessage<tl::protocol_error>(HasSubstr("negative list size")));

  EXPECT_THAT(
      [&] { w.write_map_begin(tl::ttype::i32_t, tl::ttype::i32_t, -1); },
      ThrowsMessage<tl::protocol_error>(HasSubstr("negative map size")));
}

TEST(compact_writer, encodes_long_form_header_with_multi_byte_varint_field_id) {
  std::vector<std::byte> out;
  auto w = tl::compact_writer{out};

  // Pick a field id that forces:
  //  - long-form header (delta not in 1..15 from last_field_id=0)
  //  - multi-byte varint encoding for the field id itself.
  //
  // field_id = 300:
  //   zigzag(300) = 600
  //   600 varint = 0xD8 0x04
  w.write_struct_begin({});
  w.write_field_begin({}, tl::ttype::i32_t, 300);
  w.write_i32(0);
  w.write_field_end();
  w.write_field_stop();
  w.write_struct_end();

  // long-form header for i32: ct_i32 (0x05)
  // then field id varint bytes 0xD8 0x04
  // then value i32(0): zigzag(0)=0 => 0x00
  // then STOP (0x00)
  EXPECT_THAT(as_u8s(out), ElementsAre(0x05, 0xd8, 0x04, 0x00, ct_stop));
}
