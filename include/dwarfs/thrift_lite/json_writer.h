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

#pragma once

#include <cstddef>
#include <cstdint>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

#include <dwarfs/thrift_lite/concepts.h>
#include <dwarfs/thrift_lite/types.h>
#include <dwarfs/thrift_lite/writer_options.h>

namespace dwarfs::thrift_lite {

struct json_writer_options : public writer_options {
  std::size_t indent_step = 2;
};

class json_writer final {
 public:
  explicit json_writer(std::ostream& os,
                       json_writer_options const& options = {}) noexcept;

  auto options() const -> writer_options const&;

  void write_struct_begin(std::string_view name);
  void write_struct_end();

  void
  write_field_begin(std::string_view name, ttype type, std::int16_t field_id);
  void write_field_end();
  void write_field_stop();

  void write_bool(bool v);
  void write_byte(std::int8_t v);
  void write_i16(std::int16_t v);
  void write_i32(std::int32_t v);
  void write_i64(std::int64_t v);

  void write_double(double v);

  void write_binary(std::span<std::byte const> v);
  void write_string(std::string_view v);

  void write_list_begin(ttype elem_type, std::int32_t size);
  void write_list_end();

  void write_set_begin(ttype elem_type, std::int32_t size);
  void write_set_end();

  void write_map_begin(ttype key_type, ttype val_type, std::int32_t size);
  void write_map_end();

 private:
  enum class container_kind {
    struct_k,
    list_k,
    set_k,
    map_k,
  };

  struct container_ctx {
    container_kind kind{};
    std::int32_t remaining{0};

    bool wrote_any{false};

    // struct only
    bool expecting_value{false};

    // map only
    bool expecting_key{true};
    bool key_written_in_pair{false};
  };

  void begin_value();
  void end_value();

  void write_newline_and_indent(std::size_t level);
  void write_json_string(std::string_view s);

  template <typename T>
    requires std::is_arithmetic_v<T>
  void write_number(T v);

  auto top_ctx() -> container_ctx&;

  std::ostream* os_{nullptr};
  std::size_t indent_{0};
  bool root_written_{false};
  std::vector<container_ctx> stack_;
  json_writer_options options_;
};

static_assert(protocol_writer_type<json_writer>);

} // namespace dwarfs::thrift_lite
