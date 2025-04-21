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

#include <dwarfs/error.h>
#include <dwarfs/mmif.h>
#include <dwarfs/reader/filesystem_options.h>

#include <dwarfs/reader/internal/filesystem_parser.h>

namespace dwarfs::reader::internal {

using namespace dwarfs::internal;

file_off_t
filesystem_parser::find_image_offset(mmif& mm, file_off_t image_offset) {
  if (image_offset != filesystem_options::IMAGE_OFFSET_AUTO) {
    return image_offset;
  }

  static constexpr std::array<char, 7> magic{
      {'D', 'W', 'A', 'R', 'F', 'S', MAJOR_VERSION}};

  file_off_t start = 0;
  for (;;) {
    if (start + magic.size() >= mm.size()) {
      break;
    }

    auto ss = mm.span<char>(start);
#if __cpp_lib_boyer_moore_searcher >= 201603
    auto searcher = std::boyer_moore_searcher(magic.begin(), magic.end());
#else
    auto searcher = std::default_searcher(magic.begin(), magic.end());
#endif
    auto it = std::search(ss.begin(), ss.end(), searcher);

    if (it == ss.end()) {
      break;
    }

    file_off_t pos = start + std::distance(ss.begin(), it);

    if (pos + sizeof(file_header) >= mm.size()) {
      break;
    }

    auto fh = mm.as<file_header>(pos);

    if (fh->minor < 2) {
      // v1 section header, presumably
      if (pos + sizeof(file_header) + sizeof(section_header) >= mm.size()) {
        break;
      }

      auto sh = mm.as<section_header>(pos + sizeof(file_header));

      // The only compression types supported before v0.3.0
      auto is_valid_compression = [](compression_type_v1 c) {
        return c == compression_type_v1::NONE ||
               c == compression_type_v1::LZMA ||
               c == compression_type_v1::ZSTD ||
               c == compression_type_v1::LZ4 || c == compression_type_v1::LZ4HC;
      };

      // First section must be either a block or the metadata schema,
      // using a valid compression type.
      if ((sh->type == section_type::BLOCK ||
           sh->type == section_type::METADATA_V2_SCHEMA) &&
          is_valid_compression(sh->compression) && sh->length > 0) {
        auto nextshpos =
            pos + sizeof(file_header) + sizeof(section_header) + sh->length;
        if (nextshpos + sizeof(section_header) < mm.size()) {
          auto nsh = mm.as<section_header>(nextshpos);
          // the next section must be a block or a metadata schema if the first
          // section was a block *or* a metadata block if the first section was
          // a metadata schema
          if ((sh->type == section_type::BLOCK
                   ? nsh->type == section_type::BLOCK ||
                         nsh->type == section_type::METADATA_V2_SCHEMA
                   : nsh->type == section_type::METADATA_V2) &&
              is_valid_compression(nsh->compression) && nsh->length > 0) {
            // we can be somewhat sure that this is where the filesystem starts
            return pos;
          }
        }
      }
    } else {
      // do a little more validation before we return
      if (pos + sizeof(section_header_v2) >= mm.size()) {
        break;
      }

      auto sh = mm.as<section_header_v2>(pos);

      if (sh->number == 0) {
        auto endpos = pos + sh->length + 2 * sizeof(section_header_v2);

        if (endpos >= sh->length) {
          if (endpos >= mm.size()) {
            break;
          }

          auto ps = mm.as<void>(pos + sh->length + sizeof(section_header_v2));

          if (::memcmp(ps, magic.data(), magic.size()) == 0 and
              reinterpret_cast<section_header_v2 const*>(ps)->number == 1) {
            return pos;
          }
        }
      }
    }

    start = pos + magic.size();
  }

  DWARFS_THROW(runtime_error, "no filesystem found");
}

filesystem_parser::filesystem_parser(std::shared_ptr<mmif> mm,
                                     file_off_t image_offset,
                                     file_off_t image_size)
    : mm_{std::move(mm)}
    , image_offset_{find_image_offset(*mm_, image_offset)}
    , image_size_{
          std::min<file_off_t>(image_size, mm_->size() - image_offset_)} {
  if (std::cmp_less(image_size_, sizeof(file_header))) {
    DWARFS_THROW(runtime_error, "file too small");
  }

  auto fh = mm_->as<file_header>(image_offset_);

  if (fh->magic_sv() != "DWARFS") {
    DWARFS_THROW(runtime_error, "magic not found");
  }

  if (fh->major != MAJOR_VERSION) {
    DWARFS_THROW(runtime_error, "different major version");
  }

  if (fh->minor > MINOR_VERSION) {
    DWARFS_THROW(runtime_error, "newer minor version");
  }

  version_ = fh->minor >= 2 ? 2 : 1;
  major_ = fh->major;
  minor_ = fh->minor;

  if (minor_ >= 4) {
    find_index();
  }

  rewind();
}

std::optional<fs_section> filesystem_parser::next_section() {
  if (index_.empty()) {
    if (std::cmp_less(offset_, image_offset_ + image_size_)) {
      auto section = fs_section(*mm_, offset_, version_);
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
                        image_offset_ + offset, next_offset - offset, version_);
    }
  }

  return std::nullopt;
}

std::optional<std::span<uint8_t const>> filesystem_parser::header() const {
  if (image_offset_ == 0) {
    return std::nullopt;
  }
  return mm_->span(0, image_offset_);
}

void filesystem_parser::rewind() {
  if (index_.empty()) {
    offset_ = image_offset_;
    if (version_ == 1) {
      offset_ += sizeof(file_header);
    }
  } else {
    offset_ = 0;
  }
}

std::string filesystem_parser::version() const {
  return fmt::format("{0}.{1} [{2}]", major_, minor_, version_);
}

bool filesystem_parser::has_checksums() const { return version_ >= 2; }

bool filesystem_parser::has_index() const { return !index_.empty(); }

size_t filesystem_parser::filesystem_size() const {
  return image_offset_ + image_size_;
}

std::span<uint8_t const>
filesystem_parser::section_data(fs_section const& s) const {
  return s.data(*mm_);
}

void filesystem_parser::find_index() {
  uint64_t index_pos;

  ::memcpy(&index_pos,
           mm_->as<void>(image_offset_ + image_size_ - sizeof(uint64_t)),
           sizeof(uint64_t));

  if ((index_pos >> 48) == static_cast<uint16_t>(section_type::SECTION_INDEX)) {
    index_pos &= section_offset_mask;
    index_pos += image_offset_;

    if (std::cmp_less(index_pos, image_offset_ + image_size_)) {
      auto section = fs_section(*mm_, index_pos, version_);

      if (section.check_fast(*mm_)) {
        index_.resize(section.length() / sizeof(uint64_t));
        ::memcpy(index_.data(), section.data(*mm_).data(), section.length());
      }
    }
  }
}

} // namespace dwarfs::reader::internal
