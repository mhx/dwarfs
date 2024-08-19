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

namespace cpp2 dwarfs.thrift.metadata

@cpp.Type{name = "uint8_t"}
typedef byte UInt8
@cpp.Type{name = "uint16_t"}
typedef i16 UInt16
@cpp.Type{name = "uint32_t"}
typedef i32 UInt32
@cpp.Type{name = "uint64_t"}
typedef i64 UInt64

/**
 * One chunk of data
 *
 * A single file inode can be composed of multiple chunks, e.g. because
 * segments can be reused or because a single file spans multiple blocks.
 * Chunks may be overlapping if there is identical data in different files.
 *
 * A chunk is really just a view onto an otherwise unstructured file system
 * block.
 */
struct chunk {
   1: UInt32 block        // file system block number
   2: UInt32 offset       // offset from start of block, in bytes
   3: UInt32 size         // size of chunk, in bytes
}

/**
 * One directory
 *
 * This structure represents the links between directory entries.
 * The `parent_entry` references the parent directory's `dir_entry`.
 * The `first_entry` members can be used to access the entries contained
 * in the directory.
 *
 * The range of contained entries is:
 *
 *    dir_entries[directory[inode].first_entry]
 *    ..
 *    dir_entries[directory[inode + 1].first_entry - 1]
 *
 * Note that the `first_entry` fields are stored delta-compressed
 * as of v2.3 and must be unpacked before using. Also note that
 * the `parent_entry` fields are all set to zero as of v2.3. The
 * `parent_entry` information can easily and quickly be built by
 * traversing the `dir_entries` using the unpacked `first_entry`
 * fields.
 */
struct directory {
   1: UInt32 parent_entry      // indexes into `dir_entries`

   2: UInt32 first_entry       // indexes into `dir_entries`
}

/**
 * Inode Data
 *
 * This structure contains all necessary metadata for an inode, such as
 * its mode (i.e. permissions and inode type), its owner/group and its
 * timestamps.
 */
struct inode_data {
   // index into `metadata.modes[]`
   2: UInt32 mode_index

   // index into `metadata.uids[]`
   4: UInt32 owner_index

   // index into `metadata.gids[]`
   5: UInt32 group_index

   // atime relative to `metadata.timestamp_base`
   6: UInt64 atime_offset

   // mtime relative to `metadata.timestamp_base`
   7: UInt64 mtime_offset

   // ctime relative to `metadata.timestamp_base`
   8: UInt64 ctime_offset

   /**
    * ==================================================================
    * NOTE: These fields has been deprecated with filesystem version 2.3
    *       They are still being used to read older filesystem versions.
    *       They do *not* occupy any space in version 2.3 and above.
    */

   // index into `metadata.names[]`
   1: UInt32 name_index_v2_2

   // inode number
   3: UInt32 inode_v2_2

   /* ==================================================================
    */
}

/**
 * A directory entry
 *
 * This structure represents a single directory entry and just combines
 * a name with an inode number. The inode number can then be used to
 * look up almost all other metadata.
 */
struct dir_entry {
   // index into metadata.names
   1: UInt32 name_index

   // index into metadata.inodes
   2: UInt32 inode_num
}

/**
 * File system options
 */
struct fs_options {
   // file system contains only mtime time stamps
   1: bool   mtime_only

   // time base and offsets are stored with this resolution
   // 1 = seconds, 60 = minutes, 3600 = hours, ...
   2: optional UInt32 time_resolution_sec

   3: bool   packed_chunk_table
   4: bool   packed_directories
   5: bool   packed_shared_files_table
}

/**
 * An (optionally packed) string table
 */
struct string_table {
   // raw buffer containing the concatenation of all individual,
   // potentially compressed, strings
   1: string buffer

   // symbol table for fsst compression; if fsst is not used, this
   // will not be set and `buffer` will contain uncompressed strings
   2: optional string symtab

   // the (optionally packed) index; if packed, the index is stored
   // delta-compressed
   3: list<UInt32> index

   // indicates if the index is packed
   4: bool packed_index
}

/*
 * For highly fragmented inodes, computing the size from the
 * individual chunks can be extremely slow. This cache can be
 * used to bypass the chunk lookup and size computation.
 */
struct inode_size_cache {
   // lookup from inode number to size
   1: map<UInt32, UInt64>  lookup

   // minimum number of chunks for a file to be found in the cache,
   // corresponds to scanner_options.inode_size_cache_min_chunk_count
   2: UInt64               min_chunk_count
}

/**
 * File System Metadata
 *
 * This is the root structure for all file system metadata.
 */
struct metadata {
   /**
    * Ranges of chunks that make up regular files. Identical
    * files share the same chunk range. The range of chunks
    * for a regular file are:
    *
    *   chunks[chunk_table[index]] .. chunks[chunk_table[index + 1] - 1]
    *
    * Here, `index` is either `inode - file_inode_offset` for
    * unique file inodes, or for shared file inodes:
    *
    *   shared_files[inode - file_inode_offset - unique_files] + unique_files
    *
    * Note that here `shared_files` is the unpacked version of
    * `shared_files_table`.
    */
   1: list<chunk>      chunks

   /**
    * All directories, indexed by inode number. There's one extra
    * sentinel directory at the end that has `first_entry` point to
    * the end of `dir_entries`, so directory entry lookup work the
    * same for all directories.
    *
    * Note that this list is stored in a packed format as of v2.3
    * if `options.packed_directories` is `true` and must be unpacked
    * before use. See the documentation for the `directory` struct.
    */
   2: list<directory>  directories

   /**
    * Inode metadata, indexed by inode number.
    *
    * Inodes are assigned strictly in the following order:
    *
    *   - directories, starting with the root dir at inode 0
    *   - symbolic links
    *   - unique regular files
    *   - shared regular files
    *   - character and block devices
    *   - named pipes and sockets
    *
    * The inode type can be determined from its mode, which makes
    * it possible to find the inode offsets for each distinct type
    * by a simple binary search. These inode offsets are required
    * to perform lookups into lists indexed by non-directory inode
    * numbers.
    *
    * The number of shared regular files can be determined from
    * `shared_files_table`.
    */
   3: list<inode_data> inodes

   /**
    * Chunk lookup table, indexed by `inode - file_inode_offset`.
    * There's one extra sentinel item at the end that points to the
    * end of `chunks`, so chunk lookups work the same for all inodes.
    *
    * Note that this list is stored delta-compressed as of v2.3
    * if `options.packed_chunk_table` is `true` and must be unpacked
    * before use.
    */
   4: list<UInt32>     chunk_table

   /**
    * =========================================================================
    * NOTE: This has been deprecated with filesystem version 2.3
    *       It is still being used to read older filesystem versions.
    */
   5: list<UInt32>     entry_table_v2_2
   /* =========================================================================
    */

   // symlink lookup table, indexed by `inode - symlink_inode_offset`
   6: list<UInt32>     symlink_table

   // user ids, for lookup by `inode.owner_index`
   7: list<UInt32>     uids

   // group ids, for lookup by `inode.group_index`
   8: list<UInt32>     gids

   // inode modes, for lookup by `inode.mode_index`
   9: list<UInt32>     modes

   // directory entry names, for lookup by `dir_entry.name_index`
  10: list<string>     names

   // symlink targets, for lookup by index from `symlink_table`
  11: list<string>     symlinks

   // timestamp base for all inode timestamps
  12: UInt64           timestamp_base

  /************************ DEPRECATED **********************
   *
   * These are redundant and can be determined at run-time
   * with a simple binary search. Compatibility is not
   * affected.
   *
   *   13: UInt32          chunk_inode_offset;
   *   14: UInt32          link_inode_offset;
   *
   *********************************************************/

   // file system block size in bytes
  15: UInt32           block_size

   // total file system size in bytes
  16: UInt64           total_fs_size

  //=========================================================//
  // fields added with dwarfs-0.3.0, file system version 2.1 //
  //=========================================================//

   // device ids, for lookup by `inode - device_inode_offset`
  17: optional list<UInt64>     devices

   // file system options
  18: optional fs_options       options

  //=========================================================//
  // fields added with dwarfs-0.5.0, file system version 2.3 //
  //=========================================================//

   /**
    * All directory entries
    *
    * Starting with the root directory entry at index 0, this
    * list contains ranges all directory entries of the file
    * system. Along with `directories`, this allows traversal
    * of the full file system structure.
    *
    * The ranges of entries that belong to a single directory
    * are determined by `directory.first_entry`. Within a single
    * directory, entries are ordered asciibetically by name,
    * which makes it possible to efficiently find entries using
    * binary search.
    */
  19: optional list<dir_entry>  dir_entries

   /**
    * Shared files mapping
    *
    * Note that this list is stored in a packed format if
    * `options.packed_shared_files_table` is `true` and must be
    * unpacked before use.
    *
    * In packed format, it is stored as number of repetitions
    * per index, offset by 2 (the minimum number of repetitions),
    * so e.g. a packed list
    *
    *   [0, 3, 1, 0, 1]
    *
    * would unpack to:
    *
    *   [0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 3, 3, 4, 4, 4]
    *
    * So the packed 5-element array provides mappings for 15 shared
    * file inodes. Assuming 10 unique files and a file inode offset
    * of 10, a regular file inode 25 would be a shared file inode,
    * and the index for lookup in `chunk_table` would be `10 + 1`.
    */
  20: optional list<UInt32>     shared_files_table

   // total size of hardlinked files beyond the first link, in bytes
  21: optional UInt64           total_hardlink_size

   // version string
  22: optional string           dwarfs_version

   // unix timestamp of metadata creation time
  23: optional UInt64           create_timestamp

  24: optional string_table     compact_names

  25: optional string_table     compact_symlinks

  //=========================================================//
  // fields added with dwarfs-0.7.0, file system version 2.5 //
  //=========================================================//

   // preferred path separator of original file system
  26: optional UInt32           preferred_path_separator

  //=========================================================//
  // fields added with dwarfs-0.7.3, file system version 2.5 //
  //=========================================================//

  // We don't need to increment the file system minor version
  // as file systems created with this new version are still
  // readable by older binaries as long as they don't use any
  // unsupported features (e.g. FLAC compression).

  // The set of features used in this file system image. As long
  // as an older binary supports all features, it will be able
  // to use images created with newer versions. We use strings
  // here instead of an enum so older versions can still output
  // names of features used by a newer version.
  27: optional set<string>      features

  //=========================================================//
  // fields added with dwarfs-0.8.0, file system version 2.5 //
  //=========================================================//

  // The set of categories used in this file system image. Used
  // for displaying and to select compression algorithms when
  // recompressing the image.
  28: optional list<string>     category_names

  // The category of each block in the file system image. The
  // index into this vector is the block number and the value
  // is an index into `category_names`.
  29: optional list<UInt32>     block_categories

  //==========================================================//
  // fields added with dwarfs-0.10.2, file system version 2.5 //
  //==========================================================//

  // Size cache for highly fragmented file inodes
  30: optional inode_size_cache reg_file_size_cache
}
