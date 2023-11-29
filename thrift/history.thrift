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

include "thrift/annotation/cpp.thrift"

namespace cpp2 dwarfs.thrift.history

@cpp.Type{name = "uint8_t"}
typedef byte UInt8
@cpp.Type{name = "uint16_t"}
typedef i16 UInt16
@cpp.Type{name = "uint32_t"}
typedef i32 UInt32
@cpp.Type{name = "uint64_t"}
typedef i64 UInt64

struct dwarfs_version {
   1: UInt16 major
   2: UInt16 minor
   3: UInt16 patch
   4: bool is_release
   5: optional string git_rev
   6: optional string git_branch
   7: optional string git_desc
}

struct history_entry {
   1: dwarfs_version version
   2: string system_id
   3: string compiler_id
   4: optional list<string> arguments
   5: optional UInt64 timestamp
}

struct history {
   1: list<history_entry> entries
}
