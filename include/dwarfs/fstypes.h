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

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <string>

#include <bits/types/struct_iovec.h>

#include <folly/small_vector.h>

#include "dwarfs/block_compressor.h" // TODO: or the other way round?
#include "dwarfs/checksum.h"

namespace dwarfs {

class cached_block;

// TODO: move elsewhere
class block_range {
 public:
  block_range(std::shared_ptr<cached_block const> block, size_t offset,
              size_t size);

  uint8_t const* data() const { return begin_; }
  uint8_t const* begin() const { return begin_; }
  uint8_t const* end() const { return end_; }
  size_t size() const { return end_ - begin_; }

 private:
  uint8_t const* const begin_;
  uint8_t const* const end_;
  std::shared_ptr<cached_block const> block_;
};

// TODO: move elsewhere
struct iovec_read_buf {
  // This covers more than 95% of reads
  static constexpr size_t inline_storage = 16;

  folly::small_vector<struct ::iovec, inline_storage> buf;
  folly::small_vector<block_range, inline_storage> ranges;
};

constexpr uint8_t MAJOR_VERSION = 2;
constexpr uint8_t MINOR_VERSION = 4;

enum class section_type : uint16_t {
  BLOCK = 0,
  // Optionally compressed block data.

  METADATA_V2_SCHEMA = 7,
  // Frozen metadata schema.

  METADATA_V2 = 8,
  // Frozen metadata.

  SECTION_INDEX = 9,
  // Section index.
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

struct section_header_v2 {
  char magic[6];            //  [0] "DWARFS" / file_header no longer needed
  uint8_t major;            //  [6] major version
  uint8_t minor;            //  [7] minor version
  uint8_t sha2_512_256[32]; //  [8] SHA2-512/256 starting from next field
  uint64_t xxh3_64;         // [40] XXH3-64 starting from next field
  uint32_t number;          // [48] section number
  uint16_t type;            // [52] section type
  uint16_t compression;     // [54] compression
  uint64_t length;          // [56] length of section

  static_assert(checksum::digest_size(checksum::algorithm::XXH3_64) ==
                sizeof(xxh3_64));
  static_assert(checksum::digest_size(checksum::algorithm::SHA2_512_256) ==
                sizeof(sha2_512_256));

  std::string to_string() const;
  void dump(std::ostream& os) const;
};

struct filesystem_info {
  uint64_t block_count{0};
  uint64_t compressed_block_size{0};
  uint64_t uncompressed_block_size{0};
  uint64_t compressed_metadata_size{0};
  uint64_t uncompressed_metadata_size{0};
};

bool is_valid_compression_type(compression_type type);

bool is_valid_section_type(section_type type);

std::string get_compression_name(compression_type type);

std::string get_section_name(section_type type);

} // namespace dwarfs
