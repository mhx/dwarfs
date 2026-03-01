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

#include <iterator>

#include <dwarfs/thrift_lite/varint.h>
#include <dwarfs/varint.h>

namespace dwarfs {

static_assert(varint::max_size ==
              thrift_lite::max_varint_size<varint::value_type>);

auto varint::encode(value_type value, uint8_t* const buffer) -> size_t {
  auto const begin = reinterpret_cast<std::byte*>(buffer);
  auto const end = thrift_lite::varint_encode(value, begin);
  return std::distance(begin, end);
}

auto varint::decode(std::span<uint8_t const>& buffer) -> value_type {
  auto const begin = buffer.begin();
  auto it = begin;
  auto const value = thrift_lite::varint_decode<value_type>(it, buffer.end());
  buffer = buffer.subspan(std::distance(begin, it));
  return value;
}

} // namespace dwarfs
