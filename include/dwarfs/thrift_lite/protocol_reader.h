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
#include <string>
#include <vector>

#include <dwarfs/thrift_lite/types.h>

namespace dwarfs::thrift_lite {

class protocol_reader {
 public:
  virtual ~protocol_reader() = default;

  virtual auto consumed_bytes() const noexcept -> std::size_t = 0;
  virtual auto remaining_bytes() const noexcept -> std::size_t = 0;

  virtual void read_struct_begin() = 0;
  virtual void read_struct_end() = 0;

  virtual void read_field_begin(ttype& type, std::int16_t& field_id) = 0;
  virtual void read_field_end() = 0;

  virtual auto read_bool() -> bool = 0;
  virtual auto read_byte() -> std::int8_t = 0;
  virtual auto read_i16() -> std::int16_t = 0;
  virtual auto read_i32() -> std::int32_t = 0;
  virtual auto read_i64() -> std::int64_t = 0;

  virtual auto read_double() -> double = 0;

  virtual void read_binary(std::vector<std::byte>& out) = 0;
  virtual void read_string(std::string& out) = 0;

  virtual void read_list_begin(ttype& elem_type, std::int32_t& size) = 0;
  virtual void read_list_end() = 0;

  virtual void read_set_begin(ttype& elem_type, std::int32_t& size) = 0;
  virtual void read_set_end() = 0;

  virtual void
  read_map_begin(ttype& key_type, ttype& val_type, std::int32_t& size) = 0;
  virtual void read_map_end() = 0;

  virtual void skip(ttype type) = 0;
};

} // namespace dwarfs::thrift_lite
