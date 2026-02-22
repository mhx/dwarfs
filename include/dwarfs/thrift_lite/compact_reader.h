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

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <dwarfs/thrift_lite/protocol_reader.h>
#include <dwarfs/thrift_lite/types.h>

namespace dwarfs::thrift_lite {

struct decode_options {
  std::uint32_t max_struct_depth{std::numeric_limits<std::uint32_t>::max()};
  std::uint32_t max_container_elems{std::numeric_limits<std::uint32_t>::max()};
  std::uint32_t max_string_bytes{std::numeric_limits<std::uint32_t>::max()};
};

class compact_reader : public protocol_reader {
 public:
  explicit compact_reader(std::span<std::byte const> in,
                          decode_options const& options = {});

  auto consumed_bytes() const noexcept -> std::size_t override;
  auto remaining_bytes() const noexcept -> std::size_t override;

  void read_struct_begin() override;
  void read_struct_end() override;

  void read_field_begin(ttype& type, std::int16_t& field_id) override;
  void read_field_end() override;

  auto read_bool() -> bool override;
  auto read_byte() -> std::int8_t override;
  auto read_i16() -> std::int16_t override;
  auto read_i32() -> std::int32_t override;
  auto read_i64() -> std::int64_t override;

  auto read_double() -> double override;

  void read_binary(std::vector<std::byte>& out) override;
  void read_string(std::string& out) override;

  void read_list_begin(ttype& elem_type, std::int32_t& size) override;
  void read_list_end() override;

  void read_set_begin(ttype& elem_type, std::int32_t& size) override;
  void read_set_end() override;

  void
  read_map_begin(ttype& key_type, ttype& val_type, std::int32_t& size) override;
  void read_map_end() override;

  void skip(ttype type) override;

 private:
  auto take_u8() -> std::uint8_t;
  void skip_bytes(std::size_t n);
  void ensure_available(std::size_t n) const;

  void ensure_no_pending_bool() const;

  template <std::unsigned_integral T>
  auto read_varint() -> T;

  auto read_i16_unchecked() -> std::int16_t;

  auto to_ttype(std::uint8_t compact_type) -> ttype;

  auto read_size_i32(char const* what) -> std::int32_t;

  void skip_impl(ttype type, std::uint32_t depth);

  std::span<std::byte const> in_{};
  std::size_t pos_{0};
  decode_options options_{};

  std::vector<std::int16_t> last_field_stack_{};
  std::int16_t last_field_id_{0};
  std::uint32_t struct_depth_{0};
  std::optional<bool> pending_bool_value_{};
};

} // namespace dwarfs::thrift_lite
