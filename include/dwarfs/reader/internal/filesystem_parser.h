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

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <dwarfs/file_view.h>
#include <dwarfs/fstypes.h>
#include <dwarfs/types.h>

#include <dwarfs/internal/fs_section.h>

namespace dwarfs::reader::internal {

class filesystem_parser {
 private:
  static constexpr uint64_t section_offset_mask{(UINT64_C(1) << 48) - 1};

 public:
  static file_off_t
  find_image_offset(file_view const& mm, file_off_t image_offset);

  explicit filesystem_parser(
      file_view const& mm, file_off_t image_offset = 0,
      file_off_t image_size = std::numeric_limits<file_off_t>::max());

  std::optional<dwarfs::internal::fs_section> next_section();

  std::optional<file_extents_iterable> header() const;

  void rewind();

  std::string version() const;

  int major_version() const { return fs_version_.major; }
  int minor_version() const { return fs_version_.minor; }
  int header_version() const { return header_version_; }
  filesystem_version const& fs_version() const { return fs_version_; }

  file_off_t image_offset() const { return image_offset_; }

  bool has_checksums() const;
  bool has_index() const;

  size_t filesystem_size() const;
  file_segment segment(dwarfs::internal::fs_section const& s) const;

 private:
  void find_index();

  file_view mm_;
  file_off_t const image_offset_{0};
  file_off_t const image_size_{std::numeric_limits<file_off_t>::max()};
  file_off_t offset_{0};
  int header_version_{0};
  filesystem_version fs_version_{};
  std::vector<uint64_t> index_;
};

} // namespace dwarfs::reader::internal
