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

constexpr uint8_t MAJOR_VERSION = 1;
constexpr uint8_t MINOR_VERSION = 0;

enum class section_type : uint16_t {
  BLOCK = 0,
  // Optionally compressed block data.

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
