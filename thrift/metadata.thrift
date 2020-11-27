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

/**
 * One chunk of data. A single file can be composed of multiple
 * chunks. Chunks may be overlapping if there is identical data
 * in different files.
 */
struct chunk {
   1: required UInt32 block,
   2: required UInt32 offset,
   3: required UInt32 size,
}

/**
 * One directory. This contains only a link to its parent inode
 * and a range of `entry` objects that can be looked up in
 * `metadata.entries`.
 */
struct directory {
   1: required UInt32 parent_inode,
   2: required UInt32 first_entry,
   3: required UInt32 entry_count,
}

/**
 * One entry. This can be files, directories or links. This is
 * by far the most common metadata object type, so it has been
 * optimized for size.
 */
struct entry {
   // index into metadata.names
   1: required UInt32 name_index,

   // index into metadata.modes
   2: required UInt16 mode_index,

   /**
    * Inode number. Can be used in different ways:
    *
    * - For directories, the inode can be used as an index into
    *   metadata.directories.
    * - For links, (inode - metadata.link_index_offset) can be
    *   used as an index into metadata.links.
    * - For files, (inode - metadata.chunk_index_offset) can be
    *   used as in index into metadata.chunk_index.
    */
   3: required UInt32 inode,

   // index into metadata.uids
   4: required UInt16 owner_index,

   // index into metadata.gids
   5: required UInt16 group_index,

   // atime relative to metadata.timestamp_base
   6: required UInt64 atime_offset,

   // mtime relative to metadata.timestamp_base
   7: required UInt64 mtime_offset,

   // ctime relative to metadata.timestamp_base
   8: required UInt64 ctime_offset,
}

struct metadata {
   /**
    * Ranges of chunks that make up regular files. Identical
    * files share the same inode number. The range of chunks
    * for a * regular file inode are:
    *
    *   chunks[chunk_index[inode]] .. chunks[chunk_index[inode + 1] - 1]
    */
   1: required list<chunk>     chunks,

   // all directories, indexed by inode number
   2: required list<directory> directories,

   // all entries, can be looked up by inode through entry_index
   3: required list<entry>     entries,

   // chunk index, indexed by (inode - chunk_index_offset); this
   // list has one extra item at the back that points to the end
   // of `chunks`, so chunk lookups work the same for all inodes
   4: required list<UInt32>    chunk_index,

   // entry index, indexed by inode
   5: required list<UInt32>    entry_index,

   // link index, indexed by (inode - link_index_offset)
   6: required list<UInt32>    link_index,

   // user ids, for lookup by index in entry.owner
   7: required list<UInt16>    uids,

   // group ids, for lookup by index in entry.group
   8: required list<UInt16>    gids,

   // entry modes, for lookup by index in entry.mode
   9: required list<UInt16>    modes,

   // entry names, for lookup by index in entry.name_index
  10: required list<string>    names,

   // link targets, for lookup by index from link_index
  11: required list<string>    links,

   // timestamp base for all entry timestamps
  12: required UInt64          timestamp_base,

   // inode offset for lookups into chunk_index
  13: required UInt32          chunk_index_offset;

   // inode offset for lookups into link_index
  14: required UInt32          link_index_offset;

   // block size
  15: required UInt32          block_size;

   // total file system size
  16: required UInt64          total_fs_size;
}
