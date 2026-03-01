/* vim:set ts=2 sw=2 sts=2 et: */
/**
 * \author     Marcus Holland-Moritz (github@mhxnet.de)
 * \copyright  Copyright (c) Marcus Holland-Moritz
 *
 * This file is part of dwarfs.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the “Software”), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */

#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <dwarfs/thrift_lite/internal/compact_wire.h>

#include <dwarfs/thrift_lite/assert.h>
#include <dwarfs/thrift_lite/compact_reader.h>
#include <dwarfs/thrift_lite/types.h>
#include <dwarfs/thrift_lite/varint.h>

using namespace dwarfs::thrift_lite::internal;

namespace dwarfs::thrift_lite {

compact_reader::compact_reader(std::span<std::byte const> const in,
                               decode_options const& options)
    : in_{in}
    , options_{options} {}

auto compact_reader::consumed_bytes() const noexcept -> std::size_t {
  return pos_;
}

auto compact_reader::remaining_bytes() const noexcept -> std::size_t {
  return in_.size() - pos_;
}

void compact_reader::ensure_available(std::size_t const n) const {
  if (n > remaining_bytes()) {
    throw protocol_error("unexpected end of input");
  }
}

auto compact_reader::take_u8() -> std::uint8_t {
  ensure_available(1);
  auto const b = static_cast<std::uint8_t>(in_[pos_]);
  ++pos_;
  return b;
}

void compact_reader::skip_bytes(std::size_t const n) {
  ensure_available(n);
  pos_ += n;
}

void compact_reader::ensure_no_pending_bool() const {
  if (pending_bool_value_.has_value()) {
    throw protocol_error("bool value pending: read_bool() must be called next");
  }
}

template <std::integral T>
auto compact_reader::read_varint() -> T {
  auto it = in_.begin() + static_cast<std::ptrdiff_t>(pos_);
  auto const before = it;
  auto ec = std::error_code{};
  auto const val = varint_decode<T>(it, in_.end(), ec);

  if (ec) [[unlikely]] {
    throw protocol_error("read_varint: " + ec.message());
  }

  pos_ += static_cast<std::size_t>(std::distance(before, it));

  return val;
}

auto compact_reader::read_i16_unchecked() -> std::int16_t {
  return read_varint<std::int16_t>();
}

auto compact_reader::to_ttype(std::uint8_t const compact_type) -> ttype {
  switch (compact_type) {
  case ct_stop:
    return ttype::stop_t;
  case ct_boolean_true:
  case ct_boolean_false:
    return ttype::bool_t;
  case ct_byte:
    return ttype::byte_t;
  case ct_i16:
    return ttype::i16_t;
  case ct_i32:
    return ttype::i32_t;
  case ct_i64:
    return ttype::i64_t;
  case ct_double:
    return ttype::double_t;
  case ct_binary:
    return ttype::binary_t;
  case ct_list:
    return ttype::list_t;
  case ct_set:
    return ttype::set_t;
  case ct_map:
    return ttype::map_t;
  case ct_struct:
    return ttype::struct_t;
  case ct_uuid:
    return ttype::uuid_t;
  default:
    break;
  }
  throw protocol_error("invalid compact type: " +
                       std::to_string(static_cast<int>(compact_type)));
}

void compact_reader::read_struct_begin() {
  ensure_no_pending_bool();

  if (struct_depth_ >= options_.max_struct_depth) {
    throw protocol_error("max struct depth exceeded");
  }

  last_field_stack_.push_back(last_field_id_);
  last_field_id_ = 0;
  ++struct_depth_;
}

void compact_reader::read_struct_end() {
  ensure_no_pending_bool();

  if (last_field_stack_.empty()) {
    throw protocol_error("read_struct_end without matching read_struct_begin");
  }

  last_field_id_ = last_field_stack_.back();
  last_field_stack_.pop_back();

  TL_CHECK(struct_depth_ > 0, "internal error: struct depth underflow");

  --struct_depth_;
}

void compact_reader::read_field_begin(ttype& type, std::int16_t& field_id) {
  ensure_no_pending_bool();

  auto const header = take_u8();
  if (header == ct_stop) {
    type = ttype::stop_t;
    field_id = 0;
    return;
  }

  auto const compact_type = static_cast<std::uint8_t>(header & 0x0f);
  auto const delta = static_cast<std::uint8_t>(header >> 4);

  if (delta != 0) {
    field_id = static_cast<std::int16_t>(last_field_id_ +
                                         static_cast<std::int16_t>(delta));
  } else {
    field_id = read_i16_unchecked();
  }

  // Bool fields encode the value in the compact type id nibble (TRUE/FALSE),
  // so we must surface type=bool_t and remember the value for read_bool().
  if (compact_type == ct_boolean_true) {
    pending_bool_value_ = true;
    type = ttype::bool_t;
  } else if (compact_type == ct_boolean_false) {
    pending_bool_value_ = false;
    type = ttype::bool_t;
  } else {
    type = to_ttype(compact_type);
  }

  last_field_id_ = field_id;
}

void compact_reader::read_field_end() {}

auto compact_reader::read_bool() -> bool {
  if (pending_bool_value_.has_value()) {
    auto const v = *pending_bool_value_;
    pending_bool_value_.reset();
    return v;
  }

  auto const b = take_u8();
  if (b == ct_boolean_true) {
    return true;
  }
  if (b == ct_boolean_false) {
    return false;
  }

  throw protocol_error("invalid bool encoding");
}

auto compact_reader::read_byte() -> std::int8_t {
  ensure_no_pending_bool();
  return static_cast<std::int8_t>(take_u8());
}

auto compact_reader::read_i16() -> std::int16_t {
  ensure_no_pending_bool();
  return read_i16_unchecked();
}

auto compact_reader::read_i32() -> std::int32_t {
  ensure_no_pending_bool();
  return read_varint<std::int32_t>();
}

auto compact_reader::read_i64() -> std::int64_t {
  ensure_no_pending_bool();
  return read_varint<std::int64_t>();
}

auto compact_reader::read_double() -> double {
  static_assert(sizeof(double) == sizeof(std::uint64_t),
                "unsupported double size");
  static_assert(std::numeric_limits<double>::is_iec559,
                "only IEEE 754 double supported");

  uint64_t bits{0};

  // double is encoded in little-endian
  for (int i = 0; i < 8; ++i) {
    bits |= (static_cast<uint64_t>(take_u8()) << (i * 8));
  }

  return std::bit_cast<double>(bits);
}

auto compact_reader::read_size_i32(char const* const what) -> std::int32_t {
  auto const u = read_varint<std::uint32_t>();

  if (u >
      static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())) {
    throw protocol_error(std::string{what} + " too large");
  }

  auto const size = static_cast<std::int32_t>(u);

  if (std::cmp_greater(size, options_.max_container_elems)) {
    throw protocol_error(std::string{what} + " exceeds max_container_elems");
  }

  return size;
}

auto compact_reader::read_size(std::string_view what) -> std::int32_t {
  ensure_no_pending_bool();

  auto const size = read_varint<std::uint32_t>();

  if (std::cmp_greater(size, std::numeric_limits<std::int32_t>::max())) {
    throw protocol_error(std::string{what} + " too large");
  }

  if (size > options_.max_string_bytes) {
    throw protocol_error(std::string{what} + " exceeds max_string_bytes");
  }

  return static_cast<std::int32_t>(size);
}

auto compact_reader::read_data(std::string_view what)
    -> std::span<std::byte const> {
  auto const sz = read_size(what);
  ensure_available(sz);

  auto const span = std::span{in_}.subspan(pos_, static_cast<std::size_t>(sz));
  pos_ += sz;

  return span;
}

void compact_reader::read_binary(std::vector<std::byte>& out) {
  auto const span = read_data("binary");
  out.assign(span.begin(), span.end());
}

void compact_reader::read_string(std::string& out) {
  auto const span = read_data("string");
  out.assign(reinterpret_cast<char const*>(span.data()), span.size());
}

void compact_reader::read_list_begin(ttype& elem_type, std::int32_t& size) {
  ensure_no_pending_bool();

  auto const b = take_u8();
  auto const size_nibble = static_cast<std::uint8_t>(b >> 4);
  auto const type_nibble = static_cast<std::uint8_t>(b & 0x0f);

  elem_type = to_ttype(type_nibble);

  if (size_nibble == 0x0f) {
    size = read_size_i32("list size");
  } else {
    size = static_cast<std::int32_t>(size_nibble);
  }
}

void compact_reader::read_list_end() {}

void compact_reader::read_set_begin(ttype& elem_type, std::int32_t& size) {
  // identical encoding to list
  read_list_begin(elem_type, size);
}

void compact_reader::read_set_end() {}

void compact_reader::read_map_begin(ttype& key_type, ttype& val_type,
                                    std::int32_t& size) {
  ensure_no_pending_bool();

  size = read_size_i32("map size");

  if (size == 0) {
    key_type = ttype::stop_t;
    val_type = ttype::stop_t;
    return;
  }

  auto const types = take_u8();
  auto const kt = static_cast<std::uint8_t>(types >> 4);
  auto const vt = static_cast<std::uint8_t>(types & 0x0f);

  key_type = to_ttype(kt);
  val_type = to_ttype(vt);

  if (key_type == ttype::stop_t || val_type == ttype::stop_t) {
    throw protocol_error("invalid map key/value type");
  }
}

void compact_reader::read_map_end() {}

void compact_reader::skip(ttype const type) { skip_impl(type, 0); }

void compact_reader::skip_impl(ttype const type, std::uint32_t const depth) {
  if (depth > options_.max_struct_depth) {
    throw protocol_error("max skip depth exceeded");
  }

  switch (type) {
  case ttype::stop_t:
    throw protocol_error("cannot skip stop type");
  case ttype::bool_t:
    read_bool();
    return;
  case ttype::byte_t:
    read_byte();
    return;
  case ttype::i16_t:
    read_i16();
    return;
  case ttype::i32_t:
    read_i32();
    return;
  case ttype::i64_t:
    read_i64();
    return;
  case ttype::double_t:
    read_double();
    return;
  case ttype::binary_t:
  case ttype::string_t:
    skip_bytes(read_size(type == ttype::binary_t ? "binary" : "string"));
    return;
  case ttype::uuid_t:
    ensure_no_pending_bool();
    skip_bytes(16);
    return;
  case ttype::list_t: {
    ttype elem_type{ttype::stop_t};
    std::int32_t n{0};
    read_list_begin(elem_type, n);
    for (std::int32_t i = 0; i < n; ++i) {
      skip_impl(elem_type, depth + 1);
    }
    read_list_end();
    return;
  }
  case ttype::set_t: {
    ttype elem_type{ttype::stop_t};
    std::int32_t n{0};
    read_set_begin(elem_type, n);
    for (std::int32_t i = 0; i < n; ++i) {
      skip_impl(elem_type, depth + 1);
    }
    read_set_end();
    return;
  }
  case ttype::map_t: {
    ttype key_type{ttype::stop_t};
    ttype val_type{ttype::stop_t};
    std::int32_t n{0};
    read_map_begin(key_type, val_type, n);
    for (std::int32_t i = 0; i < n; ++i) {
      skip_impl(key_type, depth + 1);
      skip_impl(val_type, depth + 1);
    }
    read_map_end();
    return;
  }
  case ttype::struct_t: {
    read_struct_begin();

    for (;;) {
      ttype ft{ttype::stop_t};
      std::int16_t fid{0};
      read_field_begin(ft, fid);
      if (ft == ttype::stop_t) {
        break;
      }
      skip_impl(ft, depth + 1);
      read_field_end();
    }

    read_struct_end();
    return;
  }
  }

  throw protocol_error("unknown ttype: " +
                       std::to_string(static_cast<int>(type)));
}

} // namespace dwarfs::thrift_lite
