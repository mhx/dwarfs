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
#include <cassert>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <dwarfs/thrift_lite/json_writer.h>

namespace dwarfs::thrift_lite {

json_writer::json_writer(std::ostream& os,
                         json_writer_options const& options) noexcept
    : os_{&os}
    , options_{options} {}

auto json_writer::options() const -> writer_options const& { return options_; }

auto json_writer::top_ctx() -> container_ctx& {
  if (stack_.empty()) {
    throw protocol_error("json_writer: internal error: empty stack");
  }
  return stack_.back();
}

void json_writer::write_newline_and_indent(std::size_t const level) {
  if (options_.indent_step == 0) {
    return;
  }
  (*os_) << '\n';
  auto const count = level * options_.indent_step;
  for (std::size_t i = 0; i < count; ++i) {
    (*os_) << ' ';
  }
}

void json_writer::write_json_string(std::string_view const s,
                                    bool const binary) {
  TL_DCHECK(binary || utf8::is_valid(s), "json_writer: invalid UTF-8 string");
  (*os_) << '"';
  for (unsigned char const c : s) {
    switch (c) {
    case '\b':
      (*os_) << "\\b";
      break;
    case '\t':
      (*os_) << "\\t";
      break;
    case '\n':
      (*os_) << "\\n";
      break;
    case '\f':
      (*os_) << "\\f";
      break;
    case '\r':
      (*os_) << "\\r";
      break;
    case '"':
      (*os_) << "\\\"";
      break;
    case '\\':
      (*os_) << "\\\\";
      break;
    default:
      if (c < 0x20 || (binary && c >= 0x7f)) {
        static constexpr auto hex = "0123456789abcdef";
        (*os_) << "\\u00" << hex[(c >> 4) & 0xf] << hex[c & 0xf];
      } else {
        os_->put(c);
      }
      break;
    }
  }
  (*os_) << '"';
}

template <typename T>
  requires std::is_arithmetic_v<T>
void json_writer::write_number(T const v) {
  if constexpr (std::is_floating_point_v<T>) {
    switch (std::fpclassify(v)) {
    case FP_INFINITE:
      if (v > 0) {
        write_string("Infinity");
      } else {
        write_string("-Infinity");
      }
      return;
    case FP_NAN:
      write_string("NaN");
      return;
    default:
      break;
    }

    assert(std::isfinite(v));
  }

  auto buf = std::array<char, 64>{};
  auto* first = buf.data();
  auto* last = buf.data() + buf.size();
  auto const res = std::to_chars(first, last, v);

  if (res.ec != std::errc{}) {
    throw protocol_error("json_writer: number formatting failed");
  }

  begin_value();
  os_->write(first, res.ptr - first);
  end_value();
}

void json_writer::begin_value() {
  if (stack_.empty()) {
    if (root_written_) {
      throw protocol_error("json_writer: multiple root values written");
    }
    return;
  }

  auto& ctx = top_ctx();

  if (ctx.kind == container_kind::struct_k) {
    if (!ctx.expecting_value) {
      throw protocol_error("json_writer: value written without matching field");
    }
    return; // field_begin already emitted separators and key/colon
  }

  if (ctx.kind == container_kind::list_k || ctx.kind == container_kind::set_k) {
    if (ctx.remaining <= 0) {
      throw protocol_error("json_writer: too many container elements written");
    }

    if (ctx.wrote_any) {
      (*os_) << ',';
    }
    write_newline_and_indent(indent_);
    return;
  }

  if (ctx.kind == container_kind::map_k) {
    if (ctx.remaining <= 0) {
      throw protocol_error("json_writer: too many map entries written");
    }

    if (ctx.expecting_key) {
      if (ctx.wrote_any) {
        (*os_) << ',';
      }
      write_newline_and_indent(indent_);
      (*os_) << '['; // begin pair
      ctx.expecting_key = false;
      ctx.key_written_in_pair = false;
      return;
    }

    // expecting value: comma is emitted after key is finished
    return;
  }

  throw protocol_error("json_writer: internal error: unknown container kind");
}

void json_writer::end_value() {
  if (stack_.empty()) {
    root_written_ = true;
    return;
  }

  auto& ctx = top_ctx();

  if (ctx.kind == container_kind::struct_k) {
    ctx.expecting_value = false;
    return;
  }

  if (ctx.kind == container_kind::list_k || ctx.kind == container_kind::set_k) {
    --ctx.remaining;
    if (ctx.remaining < 0) {
      throw protocol_error("json_writer: too many elements written");
    }
    ctx.wrote_any = true;
    return;
  }

  if (ctx.kind == container_kind::map_k) {
    if (ctx.expecting_key) {
      throw protocol_error(
          "json_writer: internal error: end_value while expecting key");
    }

    if (!ctx.key_written_in_pair) {
      // just finished key: emit comma between key and value
      (*os_) << ',';
      if (options_.indent_step != 0) {
        (*os_) << ' ';
      }
      ctx.key_written_in_pair = true;
      return;
    }

    // just finished value: close pair, count one entry
    (*os_) << ']';
    ctx.expecting_key = true;
    ctx.key_written_in_pair = false;
    --ctx.remaining;
    if (ctx.remaining < 0) {
      throw protocol_error("json_writer: too many map entries written");
    }
    ctx.wrote_any = true;
    return;
  }

  throw protocol_error("json_writer: internal error: unknown container kind");
}

void json_writer::write_struct_begin(std::string_view const /*name*/) {
  begin_value();
  (*os_) << '{';
  stack_.push_back(container_ctx{.kind = container_kind::struct_k});
  ++indent_;
}

void json_writer::write_struct_end() {
  if (stack_.empty() || top_ctx().kind != container_kind::struct_k) {
    throw protocol_error(
        "json_writer: write_struct_end without matching write_struct_begin");
  }

  auto const ctx = top_ctx();
  if (ctx.expecting_value) {
    throw protocol_error("json_writer: struct ended while expecting a value");
  }

  --indent_;
  if (ctx.wrote_any) {
    write_newline_and_indent(indent_);
  }
  (*os_) << '}';
  stack_.pop_back();
  end_value();
}

void json_writer::write_field_begin(std::string_view const name,
                                    ttype const /*type*/,
                                    std::int16_t const /*field_id*/) {
  if (stack_.empty() || top_ctx().kind != container_kind::struct_k) {
    throw protocol_error(
        "json_writer: fields can only be written inside a struct");
  }

  auto& ctx = top_ctx();
  if (ctx.expecting_value) {
    throw protocol_error(
        "json_writer: write_field_begin while expecting a value");
  }

  if (ctx.wrote_any) {
    (*os_) << ',';
  }
  write_newline_and_indent(indent_);

  write_json_string(name);
  (*os_) << ':';
  if (options_.indent_step != 0) {
    (*os_) << ' ';
  }

  ctx.expecting_value = true;
  ctx.wrote_any = true;
}

void json_writer::write_field_end() {
  if (stack_.empty() || top_ctx().kind != container_kind::struct_k) {
    throw protocol_error("json_writer: write_field_end outside of a struct");
  }

  auto& ctx = top_ctx();
  if (ctx.expecting_value) {
    throw protocol_error("json_writer: write_field_end without a value");
  }
}

void json_writer::write_field_stop() {
  // No-op for JSON: Compact protocol needs this; JSON doesn't.
}

void json_writer::write_bool(bool const v) {
  begin_value();
  (*os_) << (v ? "true" : "false");
  end_value();
}

void json_writer::write_byte(std::int8_t const v) { write_number(v); }

void json_writer::write_i16(std::int16_t const v) { write_number(v); }

void json_writer::write_i32(std::int32_t const v) { write_number(v); }

void json_writer::write_i64(std::int64_t const v) { write_number(v); }

void json_writer::write_double(double const v) { write_number(v); }

void json_writer::write_binary(std::span<std::byte const> const v) {
  begin_value();
  write_json_string(
      std::string_view{reinterpret_cast<char const*>(v.data()), v.size()},
      true);
  end_value();
}

void json_writer::write_string(std::string_view const v) {
  begin_value();
  write_json_string(v);
  end_value();
}

void json_writer::write_list_begin(ttype const /*elem_type*/,
                                   std::int32_t const size) {
  begin_value();
  (*os_) << '[';
  stack_.push_back(
      container_ctx{.kind = container_kind::list_k, .remaining = size});
  ++indent_;
}

void json_writer::write_list_end() {
  if (stack_.empty() || top_ctx().kind != container_kind::list_k) {
    throw protocol_error(
        "json_writer: write_list_end without matching write_list_begin");
  }

  auto const ctx = top_ctx();
  if (ctx.remaining != 0) {
    throw protocol_error("json_writer: not all list elements were written");
  }

  --indent_;
  if (ctx.wrote_any) {
    write_newline_and_indent(indent_);
  }
  (*os_) << ']';
  stack_.pop_back();
  end_value();
}

void json_writer::write_set_begin(ttype const /*elem_type*/,
                                  std::int32_t const size) {
  begin_value();
  (*os_) << '[';
  stack_.push_back(
      container_ctx{.kind = container_kind::set_k, .remaining = size});
  ++indent_;
}

void json_writer::write_set_end() {
  if (stack_.empty() || top_ctx().kind != container_kind::set_k) {
    throw protocol_error(
        "json_writer: write_set_end without matching write_set_begin");
  }

  auto const ctx = top_ctx();
  if (ctx.remaining != 0) {
    throw protocol_error("json_writer: not all set elements were written");
  }

  --indent_;
  if (ctx.wrote_any) {
    write_newline_and_indent(indent_);
  }
  (*os_) << ']';
  stack_.pop_back();
  end_value();
}

void json_writer::write_map_begin(ttype const /*key_type*/,
                                  ttype const /*val_type*/,
                                  std::int32_t const size) {
  begin_value();
  (*os_) << '[';
  stack_.push_back(
      container_ctx{.kind = container_kind::map_k, .remaining = size});
  ++indent_;
}

void json_writer::write_map_end() {
  if (stack_.empty() || top_ctx().kind != container_kind::map_k) {
    throw protocol_error(
        "json_writer: write_map_end without matching write_map_begin");
  }

  auto const ctx = top_ctx();

  if (!ctx.expecting_key) {
    throw protocol_error("json_writer: map ended while expecting a value");
  }
  if (ctx.remaining != 0) {
    throw protocol_error("json_writer: not all map entries were written");
  }

  --indent_;
  if (ctx.wrote_any) {
    write_newline_and_indent(indent_);
  }
  (*os_) << ']';
  stack_.pop_back();
  end_value();
}

} // namespace dwarfs::thrift_lite
