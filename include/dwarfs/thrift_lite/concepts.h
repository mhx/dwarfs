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
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <dwarfs/thrift_lite/types.h>
#include <dwarfs/thrift_lite/writer_options.h>

namespace dwarfs::thrift_lite {

template <typename T>
concept protocol_reader_type = requires(T t) {
  { t.consumed_bytes() } -> std::same_as<std::size_t>;
  { t.remaining_bytes() } -> std::same_as<std::size_t>;

  { t.read_struct_begin() } -> std::same_as<void>;
  { t.read_struct_end() } -> std::same_as<void>;

  {
    t.read_field_begin(std::declval<ttype&>(), std::declval<std::int16_t&>())
  } -> std::same_as<void>;
  { t.read_field_end() } -> std::same_as<void>;

  { t.read_bool() } -> std::same_as<bool>;
  { t.read_byte() } -> std::same_as<std::int8_t>;
  { t.read_i16() } -> std::same_as<std::int16_t>;
  { t.read_i32() } -> std::same_as<std::int32_t>;
  { t.read_i64() } -> std::same_as<std::int64_t>;

  { t.read_double() } -> std::same_as<double>;

  {
    t.read_binary(std::declval<std::vector<std::byte>&>())
  } -> std::same_as<void>;
  { t.read_string(std::declval<std::string&>()) } -> std::same_as<void>;

  {
    t.read_list_begin(std::declval<ttype&>(), std::declval<std::int32_t&>())
  } -> std::same_as<void>;
  { t.read_list_end() } -> std::same_as<void>;

  {
    t.read_set_begin(std::declval<ttype&>(), std::declval<std::int32_t&>())
  } -> std::same_as<void>;
  { t.read_set_end() } -> std::same_as<void>;

  {
    t.read_map_begin(std::declval<ttype&>(), std::declval<ttype&>(),
                     std::declval<std::int32_t&>())
  } -> std::same_as<void>;
  { t.read_map_end() } -> std::same_as<void>;

  { t.skip(std::declval<ttype>()) } -> std::same_as<void>;
};

template <typename T>
concept protocol_writer_type = requires(T t) {
  { t.options() } -> std::same_as<writer_options const&>;

  {
    t.write_struct_begin(std::declval<std::string_view>())
  } -> std::same_as<void>;
  { t.write_struct_end() } -> std::same_as<void>;

  {
    t.write_field_begin(std::declval<std::string_view>(), std::declval<ttype>(),
                        std::declval<std::int16_t>())
  } -> std::same_as<void>;
  { t.write_field_end() } -> std::same_as<void>;
  { t.write_field_stop() } -> std::same_as<void>;

  { t.write_bool(std::declval<bool>()) } -> std::same_as<void>;
  { t.write_byte(std::declval<std::int8_t>()) } -> std::same_as<void>;
  { t.write_i16(std::declval<std::int16_t>()) } -> std::same_as<void>;
  { t.write_i32(std::declval<std::int32_t>()) } -> std::same_as<void>;
  { t.write_i64(std::declval<std::int64_t>()) } -> std::same_as<void>;
  { t.write_double(std::declval<double>()) } -> std::same_as<void>;

  {
    t.write_binary(std::declval<std::span<std::byte const>>())
  } -> std::same_as<void>;
  { t.write_string(std::declval<std::string_view>()) } -> std::same_as<void>;

  {
    t.write_list_begin(std::declval<ttype>(), std::declval<std::int32_t>())
  } -> std::same_as<void>;
  { t.write_list_end() } -> std::same_as<void>;

  {
    t.write_set_begin(std::declval<ttype>(), std::declval<std::int32_t>())
  } -> std::same_as<void>;
  { t.write_set_end() } -> std::same_as<void>;

  {
    t.write_map_begin(std::declval<ttype>(), std::declval<ttype>(),
                      std::declval<std::int32_t>())
  } -> std::same_as<void>;
  { t.write_map_end() } -> std::same_as<void>;
};

} // namespace dwarfs::thrift_lite
