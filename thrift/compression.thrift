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

namespace cpp2 dwarfs.thrift.compression

typedef byte (cpp2.type = "uint8_t") UInt8
typedef i16 (cpp2.type = "uint16_t") UInt16
typedef i32 (cpp2.type = "uint32_t") UInt32
typedef i64 (cpp2.type = "uint64_t") UInt64

struct flac_block_header {
   1: UInt16 num_channels
   2: UInt8 bits_per_sample
   3: UInt8 flags
}
