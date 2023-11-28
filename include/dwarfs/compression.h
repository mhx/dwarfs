/* vim:set ts=2 sw=2 sts=2 et: */
/**
 * \author     Marcus Holland-Moritz (github@mhxnet.de)
 * \copyright  Copyright (c) Marcus Holland-Moritz
 *
 * This file is part of dwarfs.
 *
 * dwarfs is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * dwarfs is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with dwarfs.  If not, see <https://www.gnu.org/licenses/>.
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
  DWARFS_COMPRESSION_TYPE(FLAC,   6)
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
