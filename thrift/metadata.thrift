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
   1: required UInt32 parent_entry,     // indexes into dir_entries
   2: required UInt32 first_entry,      // indexes into dir_entries
}

/**
 * One entry. This can be files, directories or links. This is
 * by far the most common metadata object type, so it has been
 * optimized for size.
 */
struct inode_data {
   /**
    * =========================================================================
    * NOTE: This has been deprecated with filesystem version 2.3 (DwarFS 0.5.0)
    *       It is still being used to read older filesystem versions.
    * =========================================================================
    */
   // index into metadata.names
   1: required UInt32 name_index_v2_2,

   // index into metadata.modes
   2: required UInt16 mode_index,

   /**
    * Inode number. Can be used in different ways:
    *
    * - For directories, the inode can be used as an index into
    *   metadata.directories.
    * - For links, (inode - link_index_offset) can be
    *   used as an index into metadata.links.
    * - For files, (inode - chunk_index_offset) can be
    *   used as in index into metadata.chunk_table.
    */
   3: required UInt32 inode_v2_2,

   //--------------------------------------------------------------------------
   // TODO: actually, the inode field is redundant as of v2.3, as entries are
   //       ordered by inode already; maybe we can drop this?
   //
   //       we definitely need it for files to point into chunk_table
   //--------------------------------------------------------------------------

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

////// 
////// entries can now be stored in inode-order (we don't need old_entry_table any more :-)
////// 

struct dir_entry {                             ///// <--------- or entry?
   // index into metadata.names
   1: required UInt32 name_index,

   // index into metadata.entries
   2: required UInt32 inode_num,                  ///// <--------- entries (inodes) are shared for hardlinks
}

struct fs_options {
   // file system contains only mtime time stamps
   1: required bool   mtime_only,

   // time base and offsets are stored with this resolution
   // 1 = seconds, 60 = minutes, 3600 = hours, ...
   2: optional UInt32 time_resolution_sec,
}

struct metadata {
   /**
    * Ranges of chunks that make up regular files. Identical
    * files share the same inode number. The range of chunks
    * for a regular file inode are:
    *
    *   chunks[chunk_table[inode]] .. chunks[chunk_table[inode + 1] - 1]
    */
   1: required list<chunk>      chunks,

   /**
    * All directories, indexed by inode number. There's one extra
    * dummy directory at the end whose `first_entry` point to the
    * end of `entries`, so that directory entry lookup work the
    * same for all directories.
    */
   2: required list<directory>  directories,

   /**
    * All entries, can be looked up by inode through entry_table_v2_2, or by
    * directory through `first_entry`, where the entries will be between
    * `directories[n].first_entry` and `directories[n+1].first_entry`.
    */
   3: required list<inode_data> entries,

   /**
    * Chunk lookup table, indexed by (inode - chunk_index_offset).
    * There's one extra dummy item at the end that points to the
    * end of `chunks`, so chunk lookups work the same for all inodes.
    */
   4: required list<UInt32>     chunk_table,

   /**
    * =========================================================================
    * NOTE: This has been deprecated with filesystem version 2.3 (DwarFS 0.5.0)
    *       It is still being used to read older filesystem versions.
    * =========================================================================
    *
    * Entry lookup table, indexed by inode
    *
    * This list contains all inodes strictly in the following order:
    *
    *   - directories, starting with the root dir at inode 0
    *   - symbolic links
    *   - regular files
    *   - character and block devices
    *   - named pipes and sockets
    */
   5: required list<UInt32>     entry_table_v2_2,                     ///// <------------ deprecate (see above)

   // symlink lookup table, indexed by (inode - symlink_table_offset)
   6: required list<UInt32>     symlink_table,

   // user ids, for lookup by index in entry.owner
   7: required list<UInt16>     uids,

   // group ids, for lookup by index in entry.group
   8: required list<UInt16>     gids,

   // entry modes, for lookup by index in entry.mode
   9: required list<UInt16>     modes,

   // entry names, for lookup by index in entry.name_index
  10: required list<string>     names,

   // link targets, for lookup by index from symlink_table
  11: required list<string>     symlinks,

   // timestamp base for all entry timestamps
  12: required UInt64           timestamp_base,

  /************************ DEPRECATED **********************
   *
   * These are redundant and can be determined at run-time
   * with a simple binary search. Compatibility is not
   * affected.
   *
   *   13: required UInt32          chunk_index_offset;
   *   14: required UInt32          link_index_offset;
   *
   *********************************************************/

   // block size
  15: required UInt32           block_size,

   // total file system size
  16: required UInt64           total_fs_size,

  //=========================================================//
  // fields added with dwarfs-0.3.0, file system version 2.1 //
  //=========================================================//

   // device ids, for lookup by (inode - device_index_offset)
  17: optional list<UInt64>     devices,

   // file system options
  18: optional fs_options       options,

  //=========================================================//
  // fields added with dwarfs-0.5.0, file system version 2.3 //
  //=========================================================//

   /**
    *  TODO TODO TODO   describe this
    */
  19: optional list<dir_entry>  dir_entries,

   /**
    *  Maps from file inode to chunk_table index
    */
  20: optional list<UInt32>     shared_files_table,

   // total file system size (without hardlinks)
  21: optional UInt64           total_hardlink_size,

  // version
  22: optional string           dwarfs_version,

  // timestamp
  23: optional UInt64           create_timestamp,
}
