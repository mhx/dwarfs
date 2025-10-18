/* vim:set ts=2 sw=2 sts=2 et: */
/**
 * \author     Marcus Holland-Moritz (github@mhxnet.de)
 * \copyright  Copyright (c) Marcus Holland-Moritz
 *
 * This file is part of dwarfs.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the “Software”), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <dwarfs/block_compressor.h> // TODO: or the other way round?
#include <dwarfs/checksum.h>
#include <dwarfs/endian.h>

namespace dwarfs {

constexpr uint8_t MAJOR_VERSION = 2;
constexpr uint8_t MINOR_VERSION = 5;
constexpr uint8_t MINOR_VERSION_ACCEPTED = 6;

enum class section_type : uint16_t {
  BLOCK = 0,
  // Optionally compressed block data.

  METADATA_V2_SCHEMA = 7,
  // Frozen metadata schema.

  METADATA_V2 = 8,
  // Frozen metadata.

  SECTION_INDEX = 9,
  // Section index.

  HISTORY = 10,
  // History of file system changes.
};

struct file_header {
  std::array<char, 6> magic; // "DWARFS"
  uint8_t major;             // major version
  uint8_t minor;             // minor version

  std::string_view magic_sv() const { return {magic.data(), magic.size()}; }
};

struct section_header {
  uint16le_t type;
  compression_type_v1 compression;
  uint8_t unused;
  uint32le_t length;

  std::string to_string() const;
  void dump(std::ostream& os) const;
};

struct section_header_v2 {
  std::array<char, 6> magic; //  [0] "DWARFS" / file_header no longer needed
  uint8_t major;             //  [6] major version
  uint8_t minor;             //  [7] minor version
  std::array<uint8_t, 32>    //  [8] SHA2-512/256 starting from next field
      sha2_512_256;          //
  uint64_t xxh3_64;          // [40] XXH3-64 starting from next field
  uint32le_t number;         // [48] section number
  uint16le_t type;           // [52] section type
  uint16le_t compression;    // [54] compression
  uint64le_t length;         // [56] length of section

  std::string to_string() const;
  void dump(std::ostream& os) const;
};

struct filesystem_info {
  uint64_t block_count{0};
  uint64_t compressed_block_size{0};
  uint64_t uncompressed_block_size{0};
  uint64_t compressed_metadata_size{0};
  uint64_t uncompressed_metadata_size{0};
  bool uncompressed_block_size_is_estimate{false};
  bool uncompressed_metadata_size_is_estimate{false};
  std::vector<size_t> compressed_block_sizes;
  std::vector<std::optional<size_t>> uncompressed_block_sizes;
};

struct filesystem_version {
  uint8_t major{0};
  uint8_t minor{0};
};

bool is_known_compression_type(compression_type type);

bool is_known_section_type(section_type type);

std::string get_compression_name(compression_type type);

std::string get_section_name(section_type type);

} // namespace dwarfs
