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

#include <cstdint>

// clang-format off
#define DWARFS_COMPRESSION_TYPE_LIST(DWARFS_COMPRESSION_TYPE, SEPARATOR) \
  DWARFS_COMPRESSION_TYPE(NONE,   0) SEPARATOR                           \
  DWARFS_COMPRESSION_TYPE(LZMA,   1) SEPARATOR                           \
  DWARFS_COMPRESSION_TYPE(ZSTD,   2) SEPARATOR                           \
  DWARFS_COMPRESSION_TYPE(LZ4,    3) SEPARATOR                           \
  DWARFS_COMPRESSION_TYPE(LZ4HC,  4) SEPARATOR                           \
  DWARFS_COMPRESSION_TYPE(BROTLI, 5) SEPARATOR                           \
  DWARFS_COMPRESSION_TYPE(FLAC,   6) SEPARATOR                           \
  DWARFS_COMPRESSION_TYPE(RICEPP, 7)
// clang-format on

namespace dwarfs {

enum class compression_type_v1 : uint8_t {
#define DWARFS_COMPRESSION_TYPE_ENUMERATION_(name, value) name = value
#define DWARFS_COMMA_ ,
  DWARFS_COMPRESSION_TYPE_LIST(DWARFS_COMPRESSION_TYPE_ENUMERATION_,
                               DWARFS_COMMA_)
#undef DWARFS_COMPRESSION_TYPE_ENUMERATION_
#undef DWARFS_COMMA_
};

enum class compression_type : uint16_t {
#define DWARFS_COMPRESSION_TYPE_ENUMERATION_(name, value) name = value
#define DWARFS_COMMA_ ,
  DWARFS_COMPRESSION_TYPE_LIST(DWARFS_COMPRESSION_TYPE_ENUMERATION_,
                               DWARFS_COMMA_)
#undef DWARFS_COMPRESSION_TYPE_ENUMERATION_
#undef DWARFS_COMMA_
};

} // namespace dwarfs
