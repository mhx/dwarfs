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

#include <cstring>

#include <dwarfs/thrift_lite/compact_writer.h>
#include <dwarfs/thrift_lite/internal/compact_wire.h>

using namespace dwarfs::thrift_lite::internal;

namespace dwarfs::thrift_lite {

compact_writer::compact_writer(std::vector<std::byte>& out,
                               writer_options const& options) noexcept
    : out_{&out}
    , options_{options} {}

auto compact_writer::options() const -> writer_options const& {
  return options_;
}

void compact_writer::ensure_no_pending_bool() const {
  if (pending_bool_field_id_.has_value()) {
    throw protocol_error("bool field pending: write_bool() must follow "
                         "write_field_begin(bool_t, id)");
  }
}

void compact_writer::write_struct_begin(std::string_view) {
  ensure_no_pending_bool();
  last_field_stack_.push_back(last_field_id_);
  last_field_id_ = 0;
}

void compact_writer::write_struct_end() {
  ensure_no_pending_bool();
  if (last_field_stack_.empty()) {
    throw protocol_error(
        "write_struct_end without matching write_struct_begin");
  }
  last_field_id_ = last_field_stack_.back();
  last_field_stack_.pop_back();
}

void compact_writer::write_field_header(std::uint8_t const compact_type_id,
                                        std::int16_t const field_id) {
  auto const delta =
      static_cast<int>(field_id) - static_cast<int>(last_field_id_);

  if (field_id > last_field_id_ && delta >= 1 && delta <= 15) {
    write_u8(*out_, static_cast<std::uint8_t>((delta << 4) | compact_type_id));
  } else {
    write_u8(*out_, compact_type_id); // 0000tttt
    write_i16(field_id);
  }

  last_field_id_ = field_id;
}

void compact_writer::flush_pending_bool_field(bool const value) {
  auto const field_id = *pending_bool_field_id_;
  pending_bool_field_id_.reset();

  auto const ct = value ? ct_boolean_true : ct_boolean_false;
  write_field_header(ct, field_id);
}

void compact_writer::write_field_begin(std::string_view, ttype const type,
                                       std::int16_t const field_id) {
  ensure_no_pending_bool();

  if (type == ttype::bool_t) {
    pending_bool_field_id_ = field_id;
    return;
  }

  auto const ct = compact_type_id_for_field(type);
  write_field_header(ct, field_id);
}

void compact_writer::write_field_end() {}

void compact_writer::write_field_stop() {
  ensure_no_pending_bool();
  write_u8(*out_, ct_stop);
}

void compact_writer::write_bool(bool const v) {
  if (pending_bool_field_id_.has_value()) {
    flush_pending_bool_field(v);
    return;
  }

  write_u8(*out_, v ? ct_boolean_true : ct_boolean_false);
}

void compact_writer::write_byte(std::int8_t const v) {
  ensure_no_pending_bool();
  write_u8(*out_, static_cast<std::uint8_t>(v));
}

void compact_writer::write_i16(std::int16_t const v) {
  ensure_no_pending_bool();
  write_varint(*out_, zigzag_encode(static_cast<std::int32_t>(v)));
}

void compact_writer::write_i32(std::int32_t const v) {
  ensure_no_pending_bool();
  write_varint(*out_, zigzag_encode(v));
}

void compact_writer::write_i64(std::int64_t const v) {
  ensure_no_pending_bool();
  write_varint(*out_, zigzag_encode(v));
}

void compact_writer::write_double(double const) {
  throw protocol_error("double type not supported in this implementation");
}

void compact_writer::write_binary(std::span<std::byte const> const bytes) {
  ensure_no_pending_bool();

  if (bytes.size() >
      static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
    throw protocol_error("binary too large");
  }

  write_varint(*out_, static_cast<std::uint32_t>(bytes.size()));

  if (!bytes.empty()) {
    write_bytes(*out_, bytes.data(), bytes.size());
  }
}

void compact_writer::write_string(std::string_view const utf8) {
  write_binary(std::as_bytes(std::span(utf8.data(), utf8.size())));
}

void compact_writer::write_list_begin(ttype const elem_type,
                                      std::int32_t const size) {
  ensure_no_pending_bool();

  if (size < 0) {
    throw protocol_error("negative list size");
  }

  auto const et = compact_type_id_for_collection(elem_type);

  if (size <= 14) {
    write_u8(*out_, static_cast<std::uint8_t>((size << 4) | et));
  } else {
    write_u8(*out_, static_cast<std::uint8_t>(0xF0 | et));
    write_varint(*out_, static_cast<std::uint32_t>(size));
  }
}

void compact_writer::write_list_end() {}

void compact_writer::write_set_begin(ttype const elem_type,
                                     std::int32_t const size) {
  write_list_begin(elem_type, size);
}

void compact_writer::write_set_end() {}

void compact_writer::write_map_begin(ttype const key_type, ttype const val_type,
                                     std::int32_t const size) {
  ensure_no_pending_bool();

  if (size < 0) {
    throw protocol_error("negative map size");
  }

  write_varint(*out_, static_cast<std::uint32_t>(size));

  if (size == 0) {
    return;
  }

  auto const kt = compact_type_id_for_collection(key_type);
  auto const vt = compact_type_id_for_collection(val_type);
  write_u8(*out_, static_cast<std::uint8_t>((kt << 4) | vt));
}

void compact_writer::write_map_end() {}

} // namespace dwarfs::thrift_lite
