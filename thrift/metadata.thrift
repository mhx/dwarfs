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

namespace cpp2 dwarfs.thrift.metadata

typedef i16 (cpp2.type = "uint16_t") UInt16
typedef i32 (cpp2.type = "uint32_t") UInt32
typedef i64 (cpp2.type = "uint64_t") UInt64

struct chunk {
   1: required UInt32 block,
   2: required UInt32 offset,
   3: required UInt32 size,
}

struct directory {
   1: required UInt32 self_inode,
   2: required UInt32 parent_inode,
   3: required UInt32 first_entry,
   4: required UInt32 entry_count,
}

struct entry {
   1: required UInt32 name_index,
   2: required UInt16 mode,
   3: required UInt32 inode,
   4: required UInt16 owner,
   5: required UInt16 group,
   6: required UInt64 atime,
   7: required UInt64 mtime,
   8: required UInt64 ctime,
}

struct metadata {
   1: required list<chunk>     chunks,
   2: required list<UInt32>    chunk_index,
   3: required list<directory> directories,
   4: required list<entry>     entries,
   5: required list<UInt32>    inode_index,
   6: required list<UInt32>    dir_link_index,
   7: required list<UInt16>    uids,
   8: required list<UInt16>    gids,
   9: required list<UInt16>    modes,
  10: required list<string>    names,
  11: required list<string>    links,
  12: required UInt64          timestamp_base,
  13: required UInt32          chunk_index_offset;
  14: required UInt64          total_fs_size;
}
