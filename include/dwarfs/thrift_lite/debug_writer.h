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
#include <iosfwd>
#include <span>
#include <string_view>
#include <vector>

#include <dwarfs/thrift_lite/protocol_writer.h>
#include <dwarfs/thrift_lite/types.h>

namespace dwarfs::thrift_lite {

class debug_writer : public protocol_writer {
 public:
  explicit debug_writer(std::ostream& os) noexcept;

  void write_struct_begin(std::string_view name) override;
  void write_struct_end() override;

  void write_field_begin(std::string_view name, ttype type,
                         std::int16_t field_id) override;
  void write_field_end() override;
  void write_field_stop() override;

  void write_bool(bool v) override;
  void write_byte(std::int8_t v) override;
  void write_i16(std::int16_t v) override;
  void write_i32(std::int32_t v) override;
  void write_i64(std::int64_t v) override;

  void write_double(double) override;

  void write_string(std::string_view utf8) override;
  void write_binary(std::span<std::byte const> bytes) override;

  void write_list_begin(ttype elem_type, std::int32_t size) override;
  void write_list_end() override;

  void write_set_begin(ttype elem_type, std::int32_t size) override;
  void write_set_end() override;

  void
  write_map_begin(ttype key_type, ttype val_type, std::int32_t size) override;
  void write_map_end() override;

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
    bool first{true};

    // map only
    bool expecting_key{true};
  };

  void begin_value(ttype type);
  void end_value();

  void write_indent();

  void
  write_field_header(std::string_view name, ttype type, std::int16_t field_id);

  void write_escaped_string(std::string_view s);
  void write_hex(std::span<std::byte const> bytes, std::size_t max_bytes);

  auto& top();

  std::ostream* os_{nullptr};
  std::vector<container_ctx> stack_{};
  std::size_t indent_{0};
  std::optional<ttype> pending_value_{};
};

} // namespace dwarfs::thrift_lite
