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

#include <dwarfs/thrift_lite/assert.h>

#include <dwarfs/thrift_lite/internal/compact_wire.h>

namespace dwarfs::thrift_lite::internal {

std::uint8_t compact_type_id_for_field(ttype const t) {
  switch (t) {
  case ttype::byte_t:
    return ct_byte;
  case ttype::i16_t:
    return ct_i16;
  case ttype::i32_t:
    return ct_i32;
  case ttype::i64_t:
    return ct_i64;
  case ttype::double_t:
    return ct_double;
  case ttype::binary_t:
  case ttype::string_t:
    return ct_binary;
  case ttype::list_t:
    return ct_list;
  case ttype::set_t:
    return ct_set;
  case ttype::map_t:
    return ct_map;
  case ttype::struct_t:
    return ct_struct;
  case ttype::uuid_t:
    return ct_uuid;
  case ttype::stop_t:
    return ct_stop;
  case ttype::bool_t:
    TL_PANIC("bool_t must be encoded via field-header TRUE/FALSE or "
             "element bool encoding");
  }
  TL_PANIC("unknown ttype");
}

} // namespace dwarfs::thrift_lite::internal
