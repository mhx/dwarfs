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
#include <iostream>
#include <memory>
#include <stdexcept>

#include <sys/uio.h>

#include <folly/small_vector.h>

#include "dwarfs/block_compressor.h" // TODO: or the other way round?

namespace dwarfs {

class cached_block;

// TODO: move elsewhere
class block_range {
 public:
  block_range(std::shared_ptr<cached_block const> block, size_t offset,
              size_t size);

  const uint8_t* data() const { return begin_; }
  const uint8_t* begin() const { return begin_; }
  const uint8_t* end() const { return end_; }
  size_t size() const { return end_ - begin_; }

 private:
  const uint8_t* const begin_;
  const uint8_t* const end_;
  std::shared_ptr<cached_block const> block_;
};

struct iovec_read_buf {
  // This covers more than 95% of reads
  static constexpr size_t inline_storage = 16;

  folly::small_vector<struct ::iovec, inline_storage> buf;
  folly::small_vector<block_range, inline_storage> ranges;
};

/*************************

---------------------
  file_header
---------------------
  section_header [BLOCK]
  block 0
---------------------
  section_header [BLOCK]
  block n
---------------------
  section_header [METADATA]
  metadata
---------------------


TODO: better description ;-)

metadata:

  links_table  -> vector<uint8_t>    // links first, potential re-use for names
table :-)
  names_table  -> vector<uint8_t>
  inode_table  -> vector<chunk>      // sizeof(chunk) aligned (64-bit)
  directories...

  inode_index: inode -> dir_entry offset
  chunk_index: (inode - file_inode_offset) -> chunk offset

 *************************/

constexpr uint8_t MAJOR_VERSION = 1;
constexpr uint8_t MINOR_VERSION = 0;

enum class section_type : uint16_t {
  BLOCK = 0,
  // Optionally compressed block data.

  METADATA = 1,
  // Optionally compressed metadata. This is just
  // another section list.

  META_TABLEDATA = 2,
  // This is raw data that is indexed from the other
  // sections by offset. It contains all names, link
  // targets and chunk lists.
  // Names are referenced by offset/length. Link targets
  // are referenced by offset and actually start with a
  // uint16_t storing the length of the remaining string.
  // Names are free to share data with links targets.
  // Chunk lists are just a vector of chunks, aligned to
  // the size of a chunk for efficient access.

  META_INODE_INDEX = 3,
  // The inode index is a vector of offsets to all inodes
  // (i.e. dir_entry* structs). The vector may be offset
  // by inode_index_offset if inodes do not start at zero.

  META_CHUNK_INDEX = 4,
  // The chunk index is a vector of offsets to the start
  // of the chunk list for file inodes. As all link and
  // directory inodes precede all file inodes, this vector
  // is offset by chunk_index_offset. There is one more
  // element in the chunk index vector that holds an offset
  // to the end of the chunk lists.

  META_DIRECTORIES = 5,
  // All directory structures, in top-down order. These
  // are referenced from within the inode index. The root
  // directory also has its dir_entry* struct stored here.

  META_CONFIG = 6,
  // Configuration data for this filesystem. Defines the
  // type of dir_entry* structure being used as well as
  // the block size which is needed for working with the
  // chunk lists. Also defines inode offsets being used
  // and the total inode count (for out-of-bounds checks).

  METADATA_V2_SCHEMA = 7,
  // Frozen metadata schema.

  METADATA_V2 = 8,
  // Frozen metadata.
};

enum class dir_entry_type : uint8_t {
  DIR_ENTRY = 0,        // filesystem uses dir_entry
  DIR_ENTRY_UG = 1,     // filesystem uses dir_entry_ug
  DIR_ENTRY_UG_TIME = 2 // filesystem uses dir_entry_ug_time
};

struct file_header {
  char magic[6]; // "DWARFS"
  uint8_t major; // major version
  uint8_t minor; // minor version
};

struct section_header {
  section_type type;
  compression_type compression;
  uint8_t unused;
  uint32_t length;

  std::string to_string() const;
  void dump(std::ostream& os) const;
};

struct dir_entry { // 128 bits (16 bytes) / entry
  uint32_t name_offset;
  uint16_t name_size;
  uint16_t mode;
  uint32_t inode; // dirs start at 1, then links, then files
  union {
    uint32_t file_size; // for files only
    uint32_t offset;    // for dirs, offset to directory,
  } u;                  // for links, offset to content in link table
};

struct dir_entry_ug { // 160 bits (20 bytes) / entry
  dir_entry de;
  uint16_t owner;
  uint16_t group;
};

struct dir_entry_ug_time { // 256 bits (32 bytes) / entry
  dir_entry_ug ug;
  uint32_t atime; // yeah, I know... in a few years we can switch to 64 bits
  uint32_t mtime;
  uint32_t ctime;
};

struct directory {
  uint32_t count;
  uint32_t self;
  uint32_t parent;
  union {
    dir_entry entries[1];
    dir_entry_ug entries_ug[1];
    dir_entry_ug_time entries_ug_time[1];
  } u;
};

struct meta_config {
  uint8_t block_size_bits;
  dir_entry_type de_type;
  uint16_t unused;
  uint32_t inode_count;
  uint64_t orig_fs_size;
  uint32_t chunk_index_offset;
  uint32_t inode_index_offset;
};

using chunk_type = uint64_t;

template <unsigned BlockSizeBits>
struct chunk_access {
  static_assert(BlockSizeBits < 32, "invalid value for BlockSizeBits");

  static const unsigned block_bits = 64 - 2 * BlockSizeBits;
  static const unsigned block_shift = 64 - block_bits;
  static const chunk_type block_mask =
      (static_cast<chunk_type>(1) << block_bits) - 1;
  static const unsigned offset_shift = BlockSizeBits;
  static const chunk_type offset_mask =
      (static_cast<chunk_type>(1) << BlockSizeBits) - 1;
  static const unsigned size_shift = 0;
  static const chunk_type size_mask =
      (static_cast<chunk_type>(1) << BlockSizeBits) - 1;
  static const chunk_type max_size = size_mask + 1;

  static void set(chunk_type& chunk, size_t block, size_t offset, size_t size) {
    if (block > block_mask) {
      std::cerr << "block out of range: " << block << " > " << block_mask
                << " [" << block_bits << "]";
      throw std::runtime_error("block out of range");
    }

    if (offset > offset_mask) {
      std::cerr << "offset out of range: " << offset << " > " << offset_mask
                << " [" << block_bits << "]";
      throw std::runtime_error("offset out of range");
    }

    if (size > max_size or size == 0) {
      std::cerr << "size out of range: " << size << " > " << size_mask << " ["
                << block_bits << "]";
      throw std::runtime_error("size out of range");
    }

    chunk = (static_cast<chunk_type>(block) << block_shift) |
            (static_cast<chunk_type>(offset) << offset_shift) |
            (static_cast<chunk_type>(size - 1) << size_shift);
  }

  static size_t block(chunk_type chunk) {
    return (chunk >> block_shift) & block_mask;
  };

  static size_t offset(chunk_type chunk) {
    return (chunk >> offset_shift) & offset_mask;
  };

  static size_t size(chunk_type chunk) {
    return ((chunk >> size_shift) & size_mask) + 1;
  };
};

std::string get_compression_name(compression_type type);

std::string get_section_name(section_type type);

} // namespace dwarfs
