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

#include <array>
#include <cstddef>
#include <cstdint>
#include <ios>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <dwarfs/thrift_lite/assert.h>
#include <dwarfs/thrift_lite/debug_writer.h>
#include <dwarfs/thrift_lite/types.h>

namespace dwarfs::thrift_lite {

namespace {

[[nodiscard]] auto hex_digit(std::uint8_t const v) noexcept -> char {
  static constexpr std::array digits = {
      '0', '1', '2', '3', '4', '5', '6', '7',
      '8', '9', 'a', 'b', 'c', 'd', 'e', 'f',
  };
  return digits[v & 0x0f];
}

std::string_view type_label(ttype const type) {
  switch (type) {
  case ttype::stop_t:
    return "stop";
  case ttype::bool_t:
    return "bool";
  case ttype::byte_t:
    return "byte";
  case ttype::i16_t:
    return "i16";
  case ttype::i32_t:
    return "i32";
  case ttype::i64_t:
    return "i64";
  case ttype::double_t:
    return "double";
  case ttype::binary_t:
    return "binary";
  case ttype::string_t:
    return "string";
  case ttype::list_t:
    return "list";
  case ttype::set_t:
    return "set";
  case ttype::map_t:
    return "map";
  case ttype::struct_t:
    return "struct";
  case ttype::uuid_t:
    return "uuid";
  }

  TL_PANIC("unknown ttype: " + std::to_string(static_cast<int>(type)));
}

} // namespace

debug_writer::debug_writer(std::ostream& os,
                           writer_options const& options) noexcept
    : os_{&os}
    , options_{options} {}

auto debug_writer::options() const -> writer_options const& { return options_; }

auto& debug_writer::top() {
  TL_CHECK(!stack_.empty(), "invalid state (no container)");
  return stack_.back();
}

void debug_writer::write_indent() {
  static constexpr std::size_t width = 2;
  auto const n = indent_ * width;
  for (std::size_t i = 0; i < n; ++i) {
    (*os_) << ' ';
  }
}

void debug_writer::begin_value(ttype const type) {
  if (pending_value_) {
    if (*pending_value_ != type) {
      throw protocol_error("debug_writer: expected value of type " +
                           std::string(type_label(*pending_value_)) +
                           " but got " + std::string(type_label(type)));
    }
    pending_value_.reset();
    return;
  }

  if (stack_.empty()) {
    return;
  }

  auto& ctx = top();

  if (ctx.kind == container_kind::list_k || ctx.kind == container_kind::set_k) {
    if (ctx.remaining <= 0) {
      throw protocol_error("debug_writer: too many container elements written");
    }

    if (ctx.first) {
      (*os_) << '\n';
      ctx.first = false;
    } else {
      (*os_) << ",\n";
    }

    write_indent();
    --ctx.remaining;
    return;
  }

  if (ctx.kind == container_kind::map_k) {
    if (ctx.expecting_key) {
      if (ctx.remaining <= 0) {
        throw protocol_error("debug_writer: too many map entries written");
      }

      if (ctx.first) {
        (*os_) << '\n';
        ctx.first = false;
      } else {
        (*os_) << ",\n";
      }

      write_indent();
      ctx.expecting_key = false;
      return;
    }

    (*os_) << " -> ";
    ctx.expecting_key = true;
    --ctx.remaining;
    return;
  }
}

void debug_writer::end_value() {
  // nothing to do
}

void debug_writer::write_struct_begin(std::string_view const name) {
  begin_value(ttype::struct_t);
  (*os_) << name << " {";
  stack_.push_back(container_ctx{.kind = container_kind::struct_k});
  ++indent_;
}

void debug_writer::write_struct_end() {
  if (stack_.empty() || top().kind != container_kind::struct_k) {
    throw protocol_error(
        "debug_writer: write_struct_end without matching write_struct_begin");
  }

  auto const ctx = top();
  stack_.pop_back();
  --indent_;

  if (!ctx.first) {
    (*os_) << '\n';
    write_indent();
  }

  (*os_) << '}';
  end_value();
}

void debug_writer::write_field_header(std::string_view const name,
                                      ttype const type,
                                      std::int16_t const field_id) {
  if (type == ttype::stop_t) {
    throw protocol_error("debug_writer: field type cannot be stop");
  }

  if (pending_value_) {
    throw protocol_error("debug_writer: previous field value not written");
  }

  if (stack_.empty() || top().kind != container_kind::struct_k) {
    throw protocol_error(
        "debug_writer: fields can only be written inside a struct");
  }

  auto& ctx = top();

  if (ctx.first) {
    (*os_) << '\n';
    ctx.first = false;
  } else {
    (*os_) << ",\n";
  }

  write_indent();
  (*os_) << field_id << ": " << name << " (";

  (*os_) << type_label(type);
  (*os_) << ") = ";

  pending_value_ = type;
}

void debug_writer::write_field_begin(std::string_view const name,
                                     ttype const type,
                                     std::int16_t const field_id) {
  write_field_header(name, type, field_id);
}

void debug_writer::write_field_end() {
  if (pending_value_) {
    throw protocol_error("debug_writer: write_field_end without a value");
  }
}

void debug_writer::write_field_stop() {}

void debug_writer::write_double(double const v) {
  begin_value(ttype::double_t);
  (*os_) << v;
  end_value();
}

void debug_writer::write_bool(bool const v) {
  begin_value(ttype::bool_t);
  (*os_) << (v ? "true" : "false");
  end_value();
}

void debug_writer::write_byte(std::int8_t const v) {
  begin_value(ttype::byte_t);
  (*os_) << static_cast<int>(v);
  end_value();
}

void debug_writer::write_i16(std::int16_t const v) {
  begin_value(ttype::i16_t);
  (*os_) << v;
  end_value();
}

void debug_writer::write_i32(std::int32_t const v) {
  begin_value(ttype::i32_t);
  (*os_) << v;
  end_value();
}

void debug_writer::write_i64(std::int64_t const v) {
  begin_value(ttype::i64_t);
  (*os_) << v;
  end_value();
}

void debug_writer::write_escaped_string(std::string_view const s) {
  (*os_) << '"';
  for (auto const ch : s) {
    auto const u = static_cast<std::uint8_t>(ch);
    if (ch == '\\') {
      (*os_) << "\\\\";
    } else if (ch == '"') {
      (*os_) << "\\\"";
    } else if (ch == '\n') {
      (*os_) << "\\n";
    } else if (ch == '\r') {
      (*os_) << "\\r";
    } else if (ch == '\t') {
      (*os_) << "\\t";
    } else if (u < 0x20 || u == 0x7f) {
      (*os_) << "\\x" << hex_digit(u >> 4) << hex_digit(u);
    } else {
      (*os_) << ch;
    }
  }
  (*os_) << '"';
}

void debug_writer::write_hex(std::span<std::byte const> const bytes,
                             std::size_t const max_bytes) {
  auto const n = (bytes.size() < max_bytes) ? bytes.size() : max_bytes;
  for (std::size_t i = 0; i < n; ++i) {
    auto const u = static_cast<std::uint8_t>(bytes[i]);
    (*os_) << hex_digit(u >> 4) << hex_digit(u);
  }
  if (bytes.size() > max_bytes) {
    (*os_) << "...";
  }
}

void debug_writer::write_string(std::string_view const utf8) {
  begin_value(ttype::string_t);
  write_escaped_string(utf8);
  end_value();
}

void debug_writer::write_binary(std::span<std::byte const> const bytes) {
  begin_value(ttype::binary_t);
  (*os_) << "binary(len=" << bytes.size() << ", hex=0x";
  write_hex(bytes, 32);
  (*os_) << ')';
  end_value();
}

void debug_writer::write_list_begin(ttype const elem_type,
                                    std::int32_t const size) {
  begin_value(ttype::list_t);

  if (size < 0) {
    throw protocol_error("debug_writer: negative list size");
  }

  (*os_) << "list<" << type_label(elem_type) << ">[" << size << "] {";
  stack_.push_back(
      container_ctx{.kind = container_kind::list_k, .remaining = size});
  ++indent_;
}

void debug_writer::write_list_end() {
  if (stack_.empty() || top().kind != container_kind::list_k) {
    throw protocol_error(
        "debug_writer: write_list_end without matching write_list_begin");
  }

  auto const ctx = top();
  stack_.pop_back();
  --indent_;

  if (ctx.remaining != 0) {
    throw protocol_error("debug_writer: not all list elements were written");
  }

  if (!ctx.first) {
    (*os_) << '\n';
    write_indent();
  }

  (*os_) << '}';
  end_value();
}

void debug_writer::write_set_begin(ttype const elem_type,
                                   std::int32_t const size) {
  begin_value(ttype::set_t);

  if (size < 0) {
    throw protocol_error("debug_writer: negative set size");
  }

  (*os_) << "set<" << type_label(elem_type) << ">[" << size << "] {";
  stack_.push_back(
      container_ctx{.kind = container_kind::set_k, .remaining = size});
  ++indent_;
}

void debug_writer::write_set_end() {
  if (stack_.empty() || top().kind != container_kind::set_k) {
    throw protocol_error(
        "debug_writer: write_set_end without matching write_set_begin");
  }

  auto const ctx = top();
  stack_.pop_back();
  --indent_;

  if (ctx.remaining != 0) {
    throw protocol_error("debug_writer: not all set elements were written");
  }

  if (!ctx.first) {
    (*os_) << '\n';
    write_indent();
  }

  (*os_) << '}';
  end_value();
}

void debug_writer::write_map_begin(ttype const key_type, ttype const val_type,
                                   std::int32_t const size) {
  begin_value(ttype::map_t);

  if (size < 0) {
    throw protocol_error("debug_writer: negative map size");
  }

  (*os_) << "map<" << type_label(key_type) << ',' << type_label(val_type)
         << ">[" << size << "] {";
  stack_.push_back(container_ctx{.kind = container_kind::map_k,
                                 .remaining = size,
                                 .first = true,
                                 .expecting_key = true});
  ++indent_;
}

void debug_writer::write_map_end() {
  if (stack_.empty() || top().kind != container_kind::map_k) {
    throw protocol_error(
        "debug_writer: write_map_end without matching write_map_begin");
  }

  auto const ctx = top();
  stack_.pop_back();
  --indent_;

  if (!ctx.expecting_key) {
    throw protocol_error("debug_writer: map ended while expecting a value");
  }

  if (ctx.remaining != 0) {
    throw protocol_error("debug_writer: not all map entries were written");
  }

  if (!ctx.first) {
    (*os_) << '\n';
    write_indent();
  }

  (*os_) << '}';
  end_value();
}

} // namespace dwarfs::thrift_lite
