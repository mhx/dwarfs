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

#include <fmt/format.h>

#include "dwarfs/error.h"
#include "dwarfs/fs_section.h"
#include "dwarfs/mmif.h"

namespace dwarfs {

class fs_section_v1 : public fs_section::impl {
 public:
  fs_section_v1(mmif& mm, size_t offset);

  size_t start() const override { return start_; }
  size_t length() const override { return hdr_.length; }
  compression_type compression() const override { return hdr_.compression; }
  section_type type() const override { return hdr_.type; }
  std::string description() const override { return hdr_.to_string(); }

 private:
  size_t start_;
  section_header hdr_;
};

class fs_section_v2 : public fs_section::impl {
 public:
  fs_section_v2(mmif& mm, size_t offset);

  size_t start() const override { return start_; }
  size_t length() const override { return hdr_.length; }
  compression_type compression() const override {
    return static_cast<compression_type>(hdr_.compression);
  }
  section_type type() const override {
    return static_cast<section_type>(hdr_.type);
  }
  std::string description() const override { return hdr_.to_string(); }

 private:
  size_t start_;
  section_header_v2 hdr_;
};

fs_section::fs_section(mmif& mm, size_t offset, int version) {
  switch (version) {
  case 1:
    impl_ = std::make_shared<fs_section_v1>(mm, offset);
    break;

  case 2:
    impl_ = std::make_shared<fs_section_v2>(mm, offset);
    break;

  default:
    DWARFS_THROW(runtime_error,
                 fmt::format("unsupported section version {}", version));
    break;
  }
}

template <typename T>
void read_section_header_common(T& header, size_t& start, mmif& mm,
                                size_t offset) {
  if (offset + sizeof(T) > mm.size()) {
    DWARFS_THROW(runtime_error, "truncated section header");
  }

  ::memcpy(&header, mm.as<void>(offset), sizeof(T));

  offset += sizeof(T);

  if (offset + header.length > mm.size()) {
    DWARFS_THROW(runtime_error, "truncated section data");
  }

  start = offset;
}

fs_section_v1::fs_section_v1(mmif& mm, size_t offset) {
  read_section_header_common(hdr_, start_, mm, offset);
}

fs_section_v2::fs_section_v2(mmif& mm, size_t offset) {
  read_section_header_common(hdr_, start_, mm, offset);
}

} // namespace dwarfs
