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

// TODO: move elsewhere
struct iovec_read_buf {
  // This covers more than 95% of reads
  static constexpr size_t inline_storage = 16;

  folly::small_vector<struct ::iovec, inline_storage> buf;
  folly::small_vector<block_range, inline_storage> ranges;
};

constexpr uint8_t MAJOR_VERSION = 2;
constexpr uint8_t MINOR_VERSION = 0;

enum class section_type : uint16_t {
  BLOCK = 0,
  // Optionally compressed block data.

  METADATA_V2_SCHEMA = 7,
  // Frozen metadata schema.

  METADATA_V2 = 8,
  // Frozen metadata.
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

std::string get_compression_name(compression_type type);

std::string get_section_name(section_type type);

} // namespace dwarfs
