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
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <dwarfs/types.h>

#include <dwarfs/internal/fs_section.h>

namespace dwarfs {

class mmif;

namespace reader::internal {

class filesystem_parser {
 private:
  static constexpr uint64_t section_offset_mask{(UINT64_C(1) << 48) - 1};

 public:
  static file_off_t find_image_offset(mmif& mm, file_off_t image_offset);

  explicit filesystem_parser(
      std::shared_ptr<mmif> mm, file_off_t image_offset = 0,
      file_off_t image_size = std::numeric_limits<file_off_t>::max());

  std::optional<dwarfs::internal::fs_section> next_section();

  std::optional<std::span<uint8_t const>> header() const;

  void rewind();

  std::string version() const;

  int major_version() const { return major_; }
  int minor_version() const { return minor_; }
  int header_version() const { return version_; }

  file_off_t image_offset() const { return image_offset_; }

  bool has_checksums() const;
  bool has_index() const;

  size_t filesystem_size() const;
  std::span<uint8_t const>
  section_data(dwarfs::internal::fs_section const& s) const;

 private:
  void find_index();

  std::shared_ptr<mmif> mm_;
  file_off_t const image_offset_{0};
  file_off_t const image_size_{std::numeric_limits<file_off_t>::max()};
  file_off_t offset_{0};
  int version_{0};
  uint8_t major_{0};
  uint8_t minor_{0};
  std::vector<uint64_t> index_;
};

} // namespace reader::internal

} // namespace dwarfs
