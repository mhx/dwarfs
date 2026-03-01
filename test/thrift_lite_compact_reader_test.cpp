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
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <dwarfs/thrift_lite/compact_reader.h>
#include <dwarfs/thrift_lite/compact_writer.h>
#include <dwarfs/thrift_lite/types.h>

namespace {

using testing::ElementsAre;
using testing::HasSubstr;
using testing::ThrowsMessage;

namespace tl = dwarfs::thrift_lite;

auto make_bytes(std::initializer_list<std::uint8_t> const xs)
    -> std::vector<std::byte> {
  std::vector<std::byte> out;
  out.reserve(xs.size());
  for (auto const x : xs) {
    out.push_back(static_cast<std::byte>(x));
  }
  return out;
}

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

} // namespace

TEST(compact_reader, reads_i32_field_with_delta_header) {
  auto const bytes = make_bytes({0x15, 0x00, ct_stop}); // field 1 i32(0), STOP
  auto r = tl::compact_reader{bytes};

  r.read_struct_begin();

  auto type = tl::ttype::stop_t;
  std::int16_t fid{0};
  r.read_field_begin(type, fid);

  EXPECT_EQ(type, tl::ttype::i32_t);
  EXPECT_EQ(fid, 1);
  EXPECT_EQ(r.read_i32(), 0);
  r.read_field_end();

  r.read_field_begin(type, fid);
  EXPECT_EQ(type, tl::ttype::stop_t);

  r.read_struct_end();
  EXPECT_EQ(r.remaining_bytes(), 0U);
  EXPECT_EQ(r.consumed_bytes(), bytes.size());
}

TEST(compact_reader, reads_long_form_header_with_multi_byte_varint_field_id) {
  // long header: 0x05 (ct_i32), field_id=300 => zigzag=600 => 0xD8 0x04, value
  // i32(0)=0x00, STOP.
  auto const bytes = make_bytes({0x05, 0xd8, 0x04, 0x00, ct_stop});
  auto r = tl::compact_reader{bytes};

  r.read_struct_begin();

  auto type = tl::ttype::stop_t;
  std::int16_t fid{0};
  r.read_field_begin(type, fid);

  EXPECT_EQ(type, tl::ttype::i32_t);
  EXPECT_EQ(fid, 300);
  EXPECT_EQ(r.read_i32(), 0);
  r.read_field_end();

  r.read_field_begin(type, fid);
  EXPECT_EQ(type, tl::ttype::stop_t);

  r.read_struct_end();
  EXPECT_EQ(r.remaining_bytes(), 0U);
  EXPECT_EQ(r.consumed_bytes(), bytes.size());
}

TEST(compact_reader, reads_bool_field_in_header_and_requires_read_bool_next) {
  // field 1 bool true => (delta=1 << 4) | TRUE = 0x11, STOP
  auto const bytes = make_bytes({0x11, ct_stop});
  auto r = tl::compact_reader{bytes};

  r.read_struct_begin();

  auto type = tl::ttype::stop_t;
  std::int16_t fid{0};
  r.read_field_begin(type, fid);

  EXPECT_EQ(type, tl::ttype::bool_t);
  EXPECT_EQ(fid, 1);

  EXPECT_THAT([&] { r.read_i32(); }, ThrowsMessage<tl::protocol_error>(
                                         HasSubstr("bool value pending")));

  EXPECT_THAT(
      [&] {
        auto t2 = tl::ttype::stop_t;
        std::int16_t fid2{0};
        r.read_field_begin(t2, fid2);
      },
      ThrowsMessage<tl::protocol_error>(HasSubstr("bool value pending")));

  EXPECT_EQ(r.read_bool(), true);
  r.read_field_end();

  r.read_field_begin(type, fid);
  EXPECT_EQ(type, tl::ttype::stop_t);

  r.read_struct_end();
  EXPECT_EQ(r.remaining_bytes(), 0U);
  EXPECT_EQ(r.consumed_bytes(), bytes.size());
}

TEST(compact_reader, reads_bool_elements_in_list) {
  // list<bool> size=3: header (3<<4)|TRUE = 0x31, then 0x01 0x02 0x01
  auto const bytes =
      make_bytes({0x31, ct_boolean_true, ct_boolean_false, ct_boolean_true});
  auto r = tl::compact_reader{bytes};

  auto elem = tl::ttype::stop_t;
  std::int32_t size{0};
  r.read_list_begin(elem, size);

  EXPECT_EQ(elem, tl::ttype::bool_t);
  EXPECT_EQ(size, 3);

  EXPECT_EQ(r.read_bool(), true);
  EXPECT_EQ(r.read_bool(), false);
  EXPECT_EQ(r.read_bool(), true);

  r.read_list_end();
  EXPECT_EQ(r.remaining_bytes(), 0U);
  EXPECT_EQ(r.consumed_bytes(), bytes.size());
}

TEST(compact_reader, throws_on_invalid_bool_element_encoding) {
  // list<bool> size=1: header 0x11, then invalid element byte 0x00
  auto const bytes = make_bytes({0x11, 0x00});
  auto r = tl::compact_reader{bytes};

  auto elem = tl::ttype::stop_t;
  std::int32_t size{0};
  r.read_list_begin(elem, size);

  EXPECT_EQ(elem, tl::ttype::bool_t);
  EXPECT_EQ(size, 1);

  EXPECT_THAT([&] { r.read_bool(); }, ThrowsMessage<tl::protocol_error>(
                                          HasSubstr("invalid bool encoding")));
}

TEST(compact_reader, reads_list_long_form_when_size_exceeds_14) {
  // list<i32> size=15:
  // header 0xF5 (0xF0 | ct_i32), size varint(15)=0x0F, then i32(0..14) zigzag
  // => 0,2,...,28 (single byte)
  auto const bytes =
      make_bytes({0xf5, 0x0f, 0x00, 0x02, 0x04, 0x06, 0x08, 0x0a, 0x0c, 0x0e,
                  0x10, 0x12, 0x14, 0x16, 0x18, 0x1a, 0x1c});
  auto r = tl::compact_reader{bytes};

  auto elem = tl::ttype::stop_t;
  std::int32_t size{0};
  r.read_list_begin(elem, size);

  EXPECT_EQ(elem, tl::ttype::i32_t);
  EXPECT_EQ(size, 15);

  for (auto i = 0; i < 15; ++i) {
    EXPECT_EQ(r.read_i32(), i);
  }

  r.read_list_end();
  EXPECT_EQ(r.remaining_bytes(), 0U);
  EXPECT_EQ(r.consumed_bytes(), bytes.size());
}

TEST(compact_reader, reads_map_empty_without_consuming_types_byte) {
  // empty map: varint size=0 => 0x00, followed by sentinel 0x56 that must
  // remain unread
  auto const bytes = make_bytes({0x00, 0x56});
  auto r = tl::compact_reader{bytes};

  auto kt = tl::ttype::i32_t;
  auto vt = tl::ttype::i32_t;
  std::int32_t size{123};

  r.read_map_begin(kt, vt, size);

  EXPECT_EQ(size, 0);
  EXPECT_EQ(kt, tl::ttype::stop_t);
  EXPECT_EQ(vt, tl::ttype::stop_t);

  EXPECT_EQ(r.remaining_bytes(), 1U);
  EXPECT_EQ(r.consumed_bytes(), 1U);
  EXPECT_EQ(r.read_byte(), static_cast<std::int8_t>(0x56));
}

TEST(compact_reader, reads_map_non_empty_header_and_entries) {
  // map<i32,i64> size=1: size=0x01, type byte (ct_i32<<4)|ct_i64 = 0x56,
  // key i32(7): zigzag(7)=14 => 0x0e, val i64(9): zigzag(9)=18 => 0x12
  auto const bytes = make_bytes({0x01, 0x56, 0x0e, 0x12});
  auto r = tl::compact_reader{bytes};

  auto kt = tl::ttype::stop_t;
  auto vt = tl::ttype::stop_t;
  std::int32_t size{0};

  r.read_map_begin(kt, vt, size);

  EXPECT_EQ(size, 1);
  EXPECT_EQ(kt, tl::ttype::i32_t);
  EXPECT_EQ(vt, tl::ttype::i64_t);

  EXPECT_EQ(r.read_i32(), 7);
  EXPECT_EQ(r.read_i64(), 9);

  r.read_map_end();
  EXPECT_EQ(r.remaining_bytes(), 0U);
  EXPECT_EQ(r.consumed_bytes(), bytes.size());
}

TEST(compact_reader, throws_on_invalid_map_key_value_type) {
  // map size=1, then types byte 0x00 => key type STOP (invalid)
  auto const bytes = make_bytes({0x01, 0x00});
  auto r = tl::compact_reader{bytes};

  auto kt = tl::ttype::stop_t;
  auto vt = tl::ttype::stop_t;
  std::int32_t size{0};

  EXPECT_THAT([&] { r.read_map_begin(kt, vt, size); },
              ThrowsMessage<tl::protocol_error>(
                  HasSubstr("invalid map key/value type")));
}

TEST(compact_reader, throws_on_invalid_list_elem_type) {
  // list size=1, type STOP (invalid)
  auto const bytes = make_bytes({0x10});
  auto r = tl::compact_reader{bytes};

  auto elem = tl::ttype::stop_t;
  std::int32_t size{0};

  EXPECT_THAT([&] { r.read_list_begin(elem, size); },
              ThrowsMessage<tl::protocol_error>(
                  HasSubstr("invalid list element type")));
}

TEST(compact_reader, read_string_and_binary_enforce_max_string_bytes) {
  auto const bytes = make_bytes({0x03, static_cast<std::uint8_t>('a'),
                                 static_cast<std::uint8_t>('b'),
                                 static_cast<std::uint8_t>('c')});
  auto const opt = tl::decode_options{.max_struct_depth = 64,
                                      .max_container_elems = 1'000'000,
                                      .max_string_bytes = 2};
  auto r1 = tl::compact_reader{bytes, opt};
  auto r2 = tl::compact_reader{bytes, opt};

  std::string s;
  EXPECT_THAT([&] { r1.read_string(s); },
              ThrowsMessage<tl::protocol_error>(
                  HasSubstr("string exceeds max_string_bytes")));

  std::vector<std::byte> bin;
  EXPECT_THAT([&] { r2.read_binary(bin); },
              ThrowsMessage<tl::protocol_error>(
                  HasSubstr("binary exceeds max_string_bytes")));
}

TEST(compact_reader, read_list_begin_enforces_max_container_elems) {
  // list<i32> long form, size=2: 0xF5 0x02
  auto const bytes = make_bytes({0xf5, 0x02});
  auto const opt = tl::decode_options{.max_struct_depth = 64,
                                      .max_container_elems = 1,
                                      .max_string_bytes = 16U * 1024U * 1024U};
  auto r = tl::compact_reader{bytes, opt};

  auto elem = tl::ttype::stop_t;
  std::int32_t size{0};

  EXPECT_THAT([&] { r.read_list_begin(elem, size); },
              ThrowsMessage<tl::protocol_error>(
                  HasSubstr("list size exceeds max_container_elems")));
}

TEST(compact_reader,
     skip_skips_list_and_bool_field_and_preserves_reading_of_next_fields) {
  std::vector<std::byte> out;
  auto w = tl::compact_writer{out};

  w.write_struct_begin({});

  w.write_field_begin({}, tl::ttype::i32_t, 1);
  w.write_i32(123);
  w.write_field_end();

  w.write_field_begin({}, tl::ttype::list_t, 2);
  w.write_list_begin(tl::ttype::i16_t, 3);
  w.write_i16(1);
  w.write_i16(2);
  w.write_i16(3);
  w.write_list_end();
  w.write_field_end();

  w.write_field_begin({}, tl::ttype::bool_t, 3);
  w.write_bool(false);
  w.write_field_end();

  w.write_field_stop();
  w.write_struct_end();

  auto r = tl::compact_reader{out};

  r.read_struct_begin();

  auto type = tl::ttype::stop_t;
  std::int16_t fid{0};

  r.read_field_begin(type, fid);
  EXPECT_EQ(type, tl::ttype::i32_t);
  EXPECT_EQ(fid, 1);
  EXPECT_EQ(r.read_i32(), 123);
  r.read_field_end();

  r.read_field_begin(type, fid);
  EXPECT_EQ(type, tl::ttype::list_t);
  EXPECT_EQ(fid, 2);
  r.skip(type);
  r.read_field_end();

  r.read_field_begin(type, fid);
  EXPECT_EQ(type, tl::ttype::bool_t);
  EXPECT_EQ(fid, 3);
  r.skip(type); // consumes pending bool from header
  r.read_field_end();

  r.read_field_begin(type, fid);
  EXPECT_EQ(type, tl::ttype::stop_t);

  r.read_struct_end();
  EXPECT_EQ(r.remaining_bytes(), 0U);
  EXPECT_EQ(r.consumed_bytes(), out.size());
}

TEST(compact_reader,
     skip_skips_nested_struct_and_field_id_delta_continues_correctly) {
  std::vector<std::byte> out;
  auto w = tl::compact_writer{out};

  w.write_struct_begin({});

  w.write_field_begin({}, tl::ttype::struct_t, 1);
  w.write_struct_begin({});
  w.write_field_begin({}, tl::ttype::i32_t, 1);
  w.write_i32(1);
  w.write_field_end();
  w.write_field_stop();
  w.write_struct_end();
  w.write_field_end();

  w.write_field_begin({}, tl::ttype::i32_t, 2);
  w.write_i32(2);
  w.write_field_end();

  w.write_field_stop();
  w.write_struct_end();

  auto r = tl::compact_reader{out};

  r.read_struct_begin();

  auto type = tl::ttype::stop_t;
  std::int16_t fid{0};

  r.read_field_begin(type, fid);
  EXPECT_EQ(type, tl::ttype::struct_t);
  EXPECT_EQ(fid, 1);
  r.skip(type);
  r.read_field_end();

  r.read_field_begin(type, fid);
  EXPECT_EQ(type, tl::ttype::i32_t);
  EXPECT_EQ(fid, 2);
  EXPECT_EQ(r.read_i32(), 2);
  r.read_field_end();

  r.read_field_begin(type, fid);
  EXPECT_EQ(type, tl::ttype::stop_t);

  r.read_struct_end();
  EXPECT_EQ(r.remaining_bytes(), 0U);
  EXPECT_EQ(r.consumed_bytes(), out.size());
}

TEST(compact_reader, throws_on_unbalanced_struct_end) {
  auto const bytes = make_bytes({});
  auto r = tl::compact_reader{bytes};

  EXPECT_THAT([&] { r.read_struct_end(); },
              ThrowsMessage<tl::protocol_error>(HasSubstr(
                  "read_struct_end without matching read_struct_begin")));
}

TEST(compact_reader, throws_on_varint_u32_too_long) {
  // 6 bytes with MSB set in first 5 => reader should throw after 5 for u32
  // varint.
  auto const bytes = make_bytes({0x80, 0x80, 0x80, 0x80, 0x80, 0x00});
  auto r = tl::compact_reader{bytes};

  EXPECT_THAT([&] { r.read_i32(); }, ThrowsMessage<tl::protocol_error>(
                                         HasSubstr("integer result overflow")));
}

TEST(compact_reader, throws_on_i16_out_of_range) {
  // Use a value that zigzag-decodes to 40000 (> int16 max).
  // zigzag(40000) = 80000 => varint 0x80 0xF1 0x04
  auto const bytes = make_bytes({0x80, 0xf1, 0x04});
  auto r = tl::compact_reader{bytes};

  EXPECT_THAT([&] { r.read_i16(); }, ThrowsMessage<tl::protocol_error>(
                                         HasSubstr("integer result overflow")));
}

TEST(compact_reader, throws_on_unexpected_end_of_input) {
  // string length=2 but only 1 byte payload
  auto const bytes = make_bytes({0x02, static_cast<std::uint8_t>('a')});
  auto r = tl::compact_reader{bytes};

  std::string s;
  EXPECT_THAT(
      [&] { r.read_string(s); },
      ThrowsMessage<tl::protocol_error>(HasSubstr("unexpected end of input")));
}

TEST(compact_reader, throws_when_max_struct_depth_exceeded) {
  auto const bytes = make_bytes({});
  auto const opt = tl::decode_options{.max_struct_depth = 1,
                                      .max_container_elems = 1'000'000,
                                      .max_string_bytes = 16U * 1024U * 1024U};
  auto r = tl::compact_reader{bytes, opt};

  r.read_struct_begin();
  EXPECT_THAT([&] { r.read_struct_begin(); },
              ThrowsMessage<tl::protocol_error>(
                  HasSubstr("max struct depth exceeded")));
}

TEST(compact_reader, read_double_byte_order_is_little_endian) {
  // 1.0: sign=0, exponent=1023, significand=0 => 0x3ff0000000000000
  auto const bytes =
      make_bytes({0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf0, 0x3f});
  auto r = tl::compact_reader{bytes};
  double const d = r.read_double();
  EXPECT_DOUBLE_EQ(1.0, d);
}

TEST(compact_reader, read_double_nan_is_nan) {
  // NaN: sign=0, exponent=all 1s, significand MSB=1 => 0x7ff8000000000000
  auto const bytes =
      make_bytes({0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf8, 0x7f});
  auto r = tl::compact_reader{bytes};
  double const d = r.read_double();
  EXPECT_TRUE(std::isnan(d));
}

TEST(compact_reader, read_double_inf_is_inf) {
  // +Inf: sign=0, exponent=all 1s, significand=0 => 0x7ff0000000000000
  auto const bytes =
      make_bytes({0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf0, 0x7f});
  auto r = tl::compact_reader{bytes};
  double const d = r.read_double();
  EXPECT_DOUBLE_EQ(std::numeric_limits<double>::infinity(), d);
}

TEST(compact_reader, read_double_negative_inf_is_negative_inf) {
  // -Inf: sign=1, exponent=all 1s, significand=0 => 0xfff0000000000000
  auto const bytes =
      make_bytes({0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf0, 0xff});
  auto r = tl::compact_reader{bytes};
  double const d = r.read_double();
  EXPECT_DOUBLE_EQ(-std::numeric_limits<double>::infinity(), d);
}

TEST(compact_reader, read_binary_succeeds) {
  // length=3, then 0xaa 0xbb 0xcc
  auto const bytes = make_bytes({0x03, 0xaa, 0xbb, 0xcc});
  auto r = tl::compact_reader{bytes};

  std::vector<std::byte> bin;
  r.read_binary(bin);

  ASSERT_EQ(bin.size(), 3U);
  EXPECT_EQ(static_cast<std::uint8_t>(bin[0]), 0xaa);
  EXPECT_EQ(static_cast<std::uint8_t>(bin[1]), 0xbb);
  EXPECT_EQ(static_cast<std::uint8_t>(bin[2]), 0xcc);
  EXPECT_EQ(r.remaining_bytes(), 0U);
  EXPECT_EQ(r.consumed_bytes(), bytes.size());
}

TEST(compact_reader, read_varint_u64_loops_for_multi_byte_values) {
  // i64(1000): zigzag(1000)=2000 => varint 0xd0 0x0f
  auto const bytes = make_bytes({0xd0, 0x0f});
  auto r = tl::compact_reader{bytes};

  EXPECT_EQ(r.read_i64(), 1000);
  EXPECT_EQ(r.remaining_bytes(), 0U);
  EXPECT_EQ(r.consumed_bytes(), bytes.size());
}

TEST(compact_reader, throws_on_varint_u64_too_long) {
  // 10 bytes with MSB set => u64 varint must throw after 10 bytes
  auto const bytes =
      make_bytes({0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80});
  auto r = tl::compact_reader{bytes};

  EXPECT_THAT([&] { r.read_i64(); }, ThrowsMessage<tl::protocol_error>(
                                         HasSubstr("integer result overflow")));
}

TEST(compact_reader, list_elem_type_nibble_false_maps_to_bool_t) {
  // list<bool> size=1, element-type nibble uses FALSE (0x02) instead of TRUE
  // (0x01): header 0x12 then one element encoded as TRUE (0x01)
  auto const bytes = make_bytes({0x12, ct_boolean_true});
  auto r = tl::compact_reader{bytes};

  auto elem = tl::ttype::stop_t;
  std::int32_t size{0};
  r.read_list_begin(elem, size);

  EXPECT_EQ(elem, tl::ttype::bool_t);
  EXPECT_EQ(size, 1);
  EXPECT_EQ(r.read_bool(), true);
  r.read_list_end();
  EXPECT_EQ(r.remaining_bytes(), 0U);
  EXPECT_EQ(r.consumed_bytes(), bytes.size());
}

TEST(compact_reader, read_set_begin_parses_and_reads_elements) {
  // set<i16> size=2: header (2<<4)|ct_i16 = 0x24
  // elements: i16(1)=0x02, i16(300)=zigzag(300)=600 => 0xd8 0x04
  auto const bytes = make_bytes({0x24, 0x02, 0xd8, 0x04});
  auto r = tl::compact_reader{bytes};

  auto elem = tl::ttype::stop_t;
  std::int32_t size{0};
  r.read_set_begin(elem, size);

  EXPECT_EQ(elem, tl::ttype::i16_t);
  EXPECT_EQ(size, 2);
  EXPECT_EQ(r.read_i16(), 1);
  EXPECT_EQ(r.read_i16(), 300);

  r.read_set_end();
  EXPECT_EQ(r.remaining_bytes(), 0U);
  EXPECT_EQ(r.consumed_bytes(), bytes.size());
}

TEST(compact_reader, read_list_size_too_large_throws) {
  // long-form list header for i32: 0xf5, then size varint = 0x80000000
  // (INT32_MAX+1)
  auto const bytes = make_bytes({0xf5, 0x80, 0x80, 0x80, 0x80, 0x08});
  auto r = tl::compact_reader{bytes};

  auto elem = tl::ttype::stop_t;
  std::int32_t size{0};

  EXPECT_THAT(
      [&] { r.read_list_begin(elem, size); },
      ThrowsMessage<tl::protocol_error>(HasSubstr("list size too large")));
}

TEST(compact_reader, read_map_size_too_large_throws) {
  // map size varint = 0x80000000 (INT32_MAX+1)
  auto const bytes = make_bytes({0x80, 0x80, 0x80, 0x80, 0x08, 0x55});
  auto r = tl::compact_reader{bytes};

  auto kt = tl::ttype::stop_t;
  auto vt = tl::ttype::stop_t;
  std::int32_t size{0};

  EXPECT_THAT(
      [&] { r.read_map_begin(kt, vt, size); },
      ThrowsMessage<tl::protocol_error>(HasSubstr("map size too large")));
}

TEST(compact_reader, read_binary_too_large_throws) {
  // binary length varint = 0x80000000 (INT32_MAX+1)
  auto const bytes = make_bytes({0x80, 0x80, 0x80, 0x80, 0x08});
  auto r = tl::compact_reader{bytes};

  std::vector<std::byte> bin;
  EXPECT_THAT([&] { r.read_binary(bin); },
              ThrowsMessage<tl::protocol_error>(HasSubstr("binary too large")));
}

TEST(compact_reader, read_string_too_large_throws) {
  // string length varint = 0x80000000 (INT32_MAX+1)
  auto const bytes = make_bytes({0x80, 0x80, 0x80, 0x80, 0x08});
  auto r = tl::compact_reader{bytes};

  std::string s;
  EXPECT_THAT([&] { r.read_string(s); },
              ThrowsMessage<tl::protocol_error>(HasSubstr("string too large")));
}

TEST(compact_reader, skip_throws_on_stop_type) {
  auto const bytes = make_bytes({});
  auto r = tl::compact_reader{bytes};

  EXPECT_THAT(
      [&] { r.skip(tl::ttype::stop_t); },
      ThrowsMessage<tl::protocol_error>(HasSubstr("cannot skip stop type")));
}

TEST(compact_reader,
     skip_triggers_max_skip_depth_exceeded_with_nested_containers) {
  // Build a struct containing a list<list<i32>> field:
  // field 1: list => 0x19
  // list header: size=1 elem=list => 0x19
  // inner list header: size=1 elem=i32 => 0x15
  // i32(0) => 0x00
  // stop => 0x00
  auto const bytes = make_bytes({0x19, 0x19, 0x15, 0x00, ct_stop});
  auto const opt = tl::decode_options{
      .max_struct_depth = 1, // allow one struct scope
      .max_container_elems = 1'000'000,
      .max_string_bytes = 16U * 1024U * 1024U,
  };
  auto r = tl::compact_reader{bytes, opt};

  EXPECT_THAT(
      [&] { r.skip(tl::ttype::struct_t); },
      ThrowsMessage<tl::protocol_error>(HasSubstr("max skip depth exceeded")));
}

TEST(compact_reader,
     skip_covers_many_types_including_skip_bytes_set_map_uuid_binary) {
  std::vector<std::byte> bytes = make_bytes({
      // field 1: byte
      0x13,
      0x07,
      // field 2: i64(1000) => 0xd0 0x0f
      0x16,
      0xd0,
      0x0f,
      // field 3: binary len=3 0xaa 0xbb 0xcc
      0x18,
      0x03,
      0xaa,
      0xbb,
      0xcc,
      // field 4: uuid
      0x1d,
  });

  for (std::uint8_t i = 0; i < 16; ++i) {
    bytes.push_back(static_cast<std::byte>(i));
  }

  auto const tail = make_bytes({
      // field 5: set<i16> size=1, elem=1
      0x1a,
      0x14,
      0x02,
      // field 6: map<i32,i32> size=1, key=1 val=2
      0x1b,
      0x01,
      0x55,
      0x02,
      0x04,
      // field 7: double 1.0 => 0x3ff0000000000000
      0x17,
      0x00,
      0x00,
      0x00,
      0x00,
      0x00,
      0x00,
      0xf0,
      0x3f,
      // stop
      ct_stop,
  });
  bytes.insert(bytes.end(), tail.begin(), tail.end());

  auto r = tl::compact_reader{bytes};

  r.read_struct_begin();

  auto type = tl::ttype::stop_t;
  std::int16_t fid{0};

  r.read_field_begin(type, fid);
  EXPECT_EQ(type, tl::ttype::byte_t);
  EXPECT_EQ(fid, 1);
  r.skip(type);
  r.read_field_end();

  r.read_field_begin(type, fid);
  EXPECT_EQ(type, tl::ttype::i64_t);
  EXPECT_EQ(fid, 2);
  r.skip(type);
  r.read_field_end();

  r.read_field_begin(type, fid);
  EXPECT_EQ(type, tl::ttype::binary_t);
  EXPECT_EQ(fid, 3);
  r.skip(type); // exercises skip_bytes for binary payload
  r.read_field_end();

  r.read_field_begin(type, fid);
  EXPECT_EQ(type, tl::ttype::uuid_t);
  EXPECT_EQ(fid, 4);
  r.skip(type); // exercises skip_bytes(16)
  r.read_field_end();

  r.read_field_begin(type, fid);
  EXPECT_EQ(type, tl::ttype::set_t);
  EXPECT_EQ(fid, 5);
  r.skip(type); // exercises set_t skip path
  r.read_field_end();

  r.read_field_begin(type, fid);
  EXPECT_EQ(type, tl::ttype::map_t);
  EXPECT_EQ(fid, 6);
  r.skip(type); // exercises map_t skip path
  r.read_field_end();

  r.read_field_begin(type, fid);
  EXPECT_EQ(type, tl::ttype::double_t);
  EXPECT_EQ(fid, 7);
  r.skip(type);
  r.read_field_end();

  r.read_field_begin(type, fid);
  EXPECT_EQ(type, tl::ttype::stop_t);

  r.read_struct_end();
  EXPECT_EQ(r.remaining_bytes(), 0U);
  EXPECT_EQ(r.consumed_bytes(), bytes.size());
}

TEST(compact_reader, skip_binary_exceeds_max_string_bytes_throws) {
  // binary length=3 (0x03), but max_string_bytes=2 should reject.
  auto const bytes = make_bytes({0x03, 0xaa, 0xbb, 0xcc});
  auto const opt = tl::decode_options{
      .max_struct_depth = 64,
      .max_container_elems = 1'000'000,
      .max_string_bytes = 2,
  };
  auto r = tl::compact_reader{bytes, opt};

  EXPECT_THAT([&] { r.skip(tl::ttype::binary_t); },
              ThrowsMessage<tl::protocol_error>(
                  HasSubstr("binary exceeds max_string_bytes")));
}

TEST(compact_reader, skip_binary_too_large_throws) {
  // binary length varint = 0x80000000 (INT32_MAX+1)
  auto const bytes = make_bytes({0x80, 0x80, 0x80, 0x80, 0x08});
  auto r = tl::compact_reader{bytes};

  EXPECT_THAT([&] { r.skip(tl::ttype::binary_t); },
              ThrowsMessage<tl::protocol_error>(HasSubstr("binary too large")));
}

TEST(compact_reader, skip_of_unknown_type_throws) {
  auto const bytes = make_bytes({0xFF});
  auto r = tl::compact_reader{bytes};

  EXPECT_THAT(
      [&] { r.skip(static_cast<tl::ttype>(100)); },
      ThrowsMessage<tl::protocol_error>(HasSubstr("unknown ttype: 100")));
}

TEST(compact_reader, invalid_compact_type_in_field_header_throws) {
  auto const bytes = make_bytes({0x1F}); // field id=1, type=0x0F (invalid)
  auto r = tl::compact_reader{bytes};

  r.read_struct_begin();

  auto type = tl::ttype::stop_t;
  std::int16_t fid{0};

  EXPECT_THAT(
      [&] { r.read_field_begin(type, fid); },
      ThrowsMessage<tl::protocol_error>(HasSubstr("invalid compact type: 15")));
}
