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
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include <dwarfs/block_compressor.h>
#include <dwarfs/byte_buffer.h>
#include <dwarfs/compression_constraints.h>
#include <dwarfs/file_extents_iterable.h>
#include <dwarfs/fstypes.h>
#include <dwarfs/writer/fragment_category.h>

namespace dwarfs {

namespace internal {

class fs_section;

}

namespace writer::internal {

struct block_compression_info {
  size_t uncompressed_size{};
  std::optional<std::string> metadata;
  std::optional<compression_constraints> constraints;
};

using delayed_data_fn_type = std::move_only_function<
    std::pair<shared_byte_buffer, std::optional<std::string>>()>;

class filesystem_writer_detail {
 public:
  virtual ~filesystem_writer_detail() = default;

  using physical_block_cb_type = std::function<void(size_t)>;

  virtual void add_default_compressor(block_compressor bc) = 0;
  virtual void add_category_compressor(fragment_category::value_type cat,
                                       block_compressor bc) = 0;
  virtual void
  add_section_compressor(section_type type, block_compressor bc) = 0;
  virtual compression_constraints
  get_compression_constraints(fragment_category::value_type cat,
                              std::string const& metadata) const = 0;
  virtual block_compressor const&
  get_compressor(section_type type,
                 std::optional<fragment_category::value_type> cat =
                     std::nullopt) const = 0;
  virtual void
  configure(std::vector<fragment_category> const& expected_categories,
            size_t max_active_slots) = 0;
  virtual void
  configure_rewrite(size_t filesystem_size, size_t block_count) = 0;
  virtual void copy_header(file_extents_iterable header) = 0;
  virtual void write_block(fragment_category cat, shared_byte_buffer data,
                           physical_block_cb_type physical_block_cb,
                           std::optional<std::string> meta = std::nullopt) = 0;
  virtual void finish_category(fragment_category cat) = 0;
  virtual void write_metadata_v2_schema(shared_byte_buffer data) = 0;
  virtual void write_metadata_v2(shared_byte_buffer data) = 0;
  virtual void write_history(shared_byte_buffer data) = 0;
  virtual void check_block_compression(
      compression_type compression, std::span<uint8_t const> data,
      std::optional<fragment_category::value_type> cat = std::nullopt,
      std::optional<std::string> cat_metadata = std::nullopt,
      block_compression_info* info = nullptr) = 0;
  virtual void rewrite_section(
      dwarfs::internal::fs_section const& sec, file_segment segment,
      std::optional<fragment_category::value_type> cat = std::nullopt,
      std::optional<std::string> cat_metadata = std::nullopt) = 0;
  virtual void rewrite_block(
      delayed_data_fn_type data, size_t uncompressed_size,
      std::optional<fragment_category::value_type> cat = std::nullopt) = 0;
  virtual void write_compressed_section(dwarfs::internal::fs_section const& sec,
                                        file_segment segment) = 0;
  virtual void flush() = 0;
  virtual size_t size() const = 0;
};

} // namespace writer::internal

} // namespace dwarfs
