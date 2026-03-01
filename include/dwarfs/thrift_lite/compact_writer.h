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

#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include <dwarfs/thrift_lite/concepts.h>
#include <dwarfs/thrift_lite/types.h>
#include <dwarfs/thrift_lite/writer_options.h>

namespace dwarfs::thrift_lite {

class compact_writer final {
 public:
  explicit compact_writer(std::vector<std::byte>& out,
                          writer_options const& options = {}) noexcept;

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

  void write_binary(std::span<std::byte const> bytes);
  void write_string(std::string_view utf8);

  void write_list_begin(ttype elem_type, std::int32_t size);
  void write_list_end();

  void write_set_begin(ttype elem_type, std::int32_t size);
  void write_set_end();

  void write_map_begin(ttype key_type, ttype val_type, std::int32_t size);
  void write_map_end();

 private:
  void write_u8(std::uint8_t b);
  void write_bytes(std::span<std::byte const> bytes);
  template <std::integral Out, std::integral T>
  void write_varint(T v);
  void ensure_no_pending_bool() const;
  void write_field_header(std::uint8_t compact_type_id, std::int16_t field_id);
  void flush_pending_bool_field(bool value);

  std::vector<std::byte>* out_{nullptr};
  std::vector<std::int16_t> last_field_stack_{};
  std::int16_t last_field_id_{0};
  std::optional<std::int16_t> pending_bool_field_id_{};
  writer_options options_;
};

static_assert(protocol_writer_type<compact_writer>);

} // namespace dwarfs::thrift_lite
