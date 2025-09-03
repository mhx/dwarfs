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

#include <algorithm>
#include <array>
#include <cstring>
#include <functional>
#include <utility>
#include <version>

#include <fmt/format.h>

#include <dwarfs/binary_literals.h>
#include <dwarfs/error.h>
#include <dwarfs/file_view.h>
#include <dwarfs/reader/filesystem_options.h>

#include <dwarfs/reader/internal/filesystem_parser.h>

namespace dwarfs::reader::internal {

using namespace dwarfs::internal;
using namespace dwarfs::binary_literals;

namespace {

constexpr std::array<char, 7> kMagic{
    {'D', 'W', 'A', 'R', 'F', 'S', MAJOR_VERSION}};

inline size_t search_dwarfs_header(std::span<std::byte const> haystack) {
  size_t const n = haystack.size();

  if (n < kMagic.size()) {
    return n;
  }

  auto const* base = reinterpret_cast<unsigned char const*>(haystack.data());
  auto const* p = base + 6;         // gate on major version
  auto const* limit = base + n - 1; // inclusive end for memchr

  int const gate = static_cast<int>(kMagic[6]);

  while (p <= limit) {
    auto const* hit = std::memchr(p, gate, static_cast<size_t>(limit - p + 1));

    if (!hit) {
      break;
    }

    auto const* i = static_cast<unsigned char const*>(hit);
    auto const* cand = i - 6;

    if (cand[0] == kMagic[0] && cand[1] == kMagic[1] && cand[2] == kMagic[2] &&
        cand[3] == kMagic[3] && cand[4] == kMagic[4] && cand[5] == kMagic[5]) {
      return static_cast<size_t>(cand - base);
    }

    p = i + 1;
  }

  return n;
}

file_off_t search_image_in_segment(file_segment const& seg) {
  file_off_t start = 0;

  while (start + kMagic.size() < seg.size()) {
    auto ss = seg.span(start);
    auto dp = search_dwarfs_header(ss);

    if (dp >= ss.size()) {
      break;
    }

    file_off_t pos = start + dp;

    if (pos + sizeof(file_header) >= seg.size()) {
      break;
    }

    auto fh = seg.read<file_header>(pos);

    if (fh.minor < 2) {
      // v1 section header, presumably
      if (pos + sizeof(file_header) + sizeof(section_header) >= seg.size()) {
        break;
      }

      auto sh = seg.read<section_header>(pos + sizeof(file_header));

      // The only compression types supported before v0.3.0
      auto is_valid_compression = [](compression_type_v1 c) {
        return c == compression_type_v1::NONE ||
               c == compression_type_v1::LZMA ||
               c == compression_type_v1::ZSTD ||
               c == compression_type_v1::LZ4 || c == compression_type_v1::LZ4HC;
      };

      // First section must be either a block or the metadata schema,
      // using a valid compression type.
      auto const shtype = static_cast<section_type>(sh.type);
      if ((shtype == section_type::BLOCK ||
           shtype == section_type::METADATA_V2_SCHEMA) &&
          is_valid_compression(sh.compression) && sh.length > 0) {
        auto nextshpos =
            pos + sizeof(file_header) + sizeof(section_header) + sh.length;
        if (nextshpos + sizeof(section_header) < seg.size()) {
          auto nsh = seg.read<section_header>(nextshpos);
          auto const nshtype = static_cast<section_type>(nsh.type);
          // the next section must be a block or a metadata schema if the first
          // section was a block *or* a metadata block if the first section was
          // a metadata schema
          if ((shtype == section_type::BLOCK
                   ? nshtype == section_type::BLOCK ||
                         nshtype == section_type::METADATA_V2_SCHEMA
                   : nshtype == section_type::METADATA_V2) &&
              is_valid_compression(nsh.compression) && nsh.length > 0) {
            // we can be somewhat sure that this is where the filesystem starts
            return pos;
          }
        }
      }
    } else {
      // do a little more validation before we return
      if (pos + sizeof(section_header_v2) >= seg.size()) {
        break;
      }

      auto sh = seg.read<section_header_v2>(pos);

      if (sh.number == 0) {
        auto endpos = pos + sh.length + 2 * sizeof(section_header_v2);

        if (endpos >= sh.length) {
          if (endpos >= seg.size()) {
            break;
          }

          auto nsh = seg.read<section_header_v2>(pos + sh.length +
                                                 sizeof(section_header_v2));

          if (::memcmp(&nsh, kMagic.data(), kMagic.size()) == 0 and
              nsh.number == 1) {
            return pos;
          }
        }
      }
    }

    start = pos + kMagic.size();
  }

  return -1;
}

} // namespace

file_off_t filesystem_parser::find_image_offset(file_view const& mm,
                                                file_off_t image_offset) {
  if (image_offset != filesystem_options::IMAGE_OFFSET_AUTO) {
    return image_offset;
  }

  for (auto const& ext : mm.extents()) {
    if (ext.kind() == extent_kind::hole) {
      // skip holes
      continue;
    }

    for (auto const& seg : ext.segments(8_MiB, sizeof(section_header_v2))) {
      auto pos = search_image_in_segment(seg);

      if (pos >= 0) {
        return seg.offset() + pos;
      }
    }
  }

  DWARFS_THROW(runtime_error, "no filesystem found");
}

filesystem_parser::filesystem_parser(file_view const& mm,
                                     file_off_t image_offset,
                                     file_off_t image_size)
    : mm_{mm}
    , image_offset_{find_image_offset(mm_, image_offset)}
    , image_size_{
          std::min<file_off_t>(image_size, mm_.size() - image_offset_)} {
  if (std::cmp_less(image_size_, sizeof(file_header))) {
    DWARFS_THROW(runtime_error, "filesystem image too small");
  }

  auto fh = mm_.read<file_header>(image_offset_);

  if (fh.magic_sv() != "DWARFS") {
    DWARFS_THROW(runtime_error, "magic not found");
  }

  if (fh.major != MAJOR_VERSION) {
    DWARFS_THROW(runtime_error, "unsupported major version");
  }

  if (fh.minor > MINOR_VERSION) {
    DWARFS_THROW(runtime_error, "unsupported minor version");
  }

  header_version_ = fh.minor >= 2 ? 2 : 1;
  fs_version_.major = fh.major;
  fs_version_.minor = fh.minor;

  if (fs_version_.minor >= 4) {
    find_index();
  }

  rewind();
}

std::optional<fs_section> filesystem_parser::next_section() {
  if (index_.empty()) {
    if (std::cmp_less(offset_, image_offset_ + image_size_)) {
      auto section = fs_section(mm_, offset_, header_version_);
      offset_ = section.end();
      return section;
    }
  } else {
    if (std::cmp_less(offset_, index_.size())) {
      uint64_t id = index_[offset_++];
      uint64_t offset = id & section_offset_mask;
      uint64_t next_offset = std::cmp_less(offset_, index_.size())
                                 ? index_[offset_] & section_offset_mask
                                 : image_size_;
      return fs_section(mm_, static_cast<section_type>(id >> 48),
                        image_offset_ + offset, next_offset - offset,
                        header_version_);
    }
  }

  return std::nullopt;
}

std::optional<std::span<uint8_t const>> filesystem_parser::header() const {
  if (image_offset_ == 0) {
    return std::nullopt;
  }
  return mm_.raw_bytes<uint8_t>(0, image_offset_);
}

void filesystem_parser::rewind() {
  if (index_.empty()) {
    offset_ = image_offset_;
    if (header_version_ == 1) {
      offset_ += sizeof(file_header);
    }
  } else {
    offset_ = 0;
  }
}

std::string filesystem_parser::version() const {
  return fmt::format("{0}.{1} [{2}]", fs_version_.major, fs_version_.minor,
                     header_version_);
}

bool filesystem_parser::has_checksums() const { return header_version_ >= 2; }

bool filesystem_parser::has_index() const { return !index_.empty(); }

size_t filesystem_parser::filesystem_size() const {
  return image_offset_ + image_size_;
}

std::span<uint8_t const>
filesystem_parser::section_data(fs_section const& s) const {
  return s.raw_bytes(mm_);
}

void filesystem_parser::find_index() {
  uint64_t index_pos =
      mm_.read<uint64le_t>(image_offset_ + image_size_ - sizeof(uint64le_t))
          .load();

  if ((index_pos >> 48) != static_cast<uint16_t>(section_type::SECTION_INDEX)) {
    return;
  }

  index_pos &= section_offset_mask;
  index_pos += image_offset_;

  if (std::cmp_greater_equal(index_pos, image_offset_ + image_size_)) {
    return;
  }

  auto section = fs_section(mm_, index_pos, header_version_);

  if (section.type() != section_type::SECTION_INDEX) {
    return;
  }

  if (section.compression() != compression_type::NONE) {
    return;
  }

  if (section.length() % sizeof(uint64_t) != 0) {
    return;
  }

  auto seg = section.segment(mm_);

  if (!section.check_fast(seg)) {
    return;
  }

  auto const section_count = section.length() / sizeof(uint64_t);

  // at least METADATA_V2_SCHEMA, METADATA_V2, and SECTION_INDEX
  if (section_count < 3) {
    return;
  }

  auto const index = section.raw_bytes(mm_);

  std::vector<uint64le_t> tmp(section_count);
  index_.resize(section_count);

  ::memcpy(tmp.data(), index.data(), index.size());
  std::ranges::transform(tmp, index_.begin(),
                         [](auto const& v) { return v.load(); });

  // index entries must be sorted by offset
  if (!std::ranges::is_sorted(index_, [](auto const a, auto const b) {
        return (a & section_offset_mask) < (b & section_offset_mask);
      })) {
    // remove the index again if it is not sorted
    index_.clear();
    return;
  }

  if ((index_.at(0) & section_offset_mask) != 0) {
    index_.clear();
    return;
  }
}

} // namespace dwarfs::reader::internal
