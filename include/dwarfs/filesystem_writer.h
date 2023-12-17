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
#include <memory>
#include <ostream>
#include <span>
#include <utility>
#include <vector>

#include <folly/Function.h>

#include "dwarfs/compression_constraints.h"
#include "dwarfs/fragment_category.h"
#include "dwarfs/fs_section.h"
#include "dwarfs/fstypes.h"
#include "dwarfs/options.h"
#include "dwarfs/worker_group.h"

namespace dwarfs {

class block_compressor;
class block_data;
class logger;
class progress;
class worker_group;

class filesystem_writer {
 public:
  using physical_block_cb_type = folly::Function<void(size_t)>;

  filesystem_writer(
      std::ostream& os, logger& lgr, worker_group& wg, progress& prog,
      block_compressor const& schema_bc, block_compressor const& metadata_bc,
      block_compressor const& history_bc,
      filesystem_writer_options const& options = filesystem_writer_options(),
      std::istream* header = nullptr);

  void add_default_compressor(block_compressor bc) {
    impl_->add_default_compressor(std::move(bc));
  }

  void add_category_compressor(fragment_category::value_type cat,
                               block_compressor bc) {
    impl_->add_category_compressor(cat, std::move(bc));
  }

  compression_constraints
  get_compression_constraints(fragment_category::value_type cat,
                              std::string const& metadata) const {
    return impl_->get_compression_constraints(cat, metadata);
  }

  block_compressor const& get_compressor(
      section_type type,
      std::optional<fragment_category::value_type> cat = std::nullopt) const {
    return impl_->get_compressor(type, cat);
  }

  void configure(std::vector<fragment_category> const& expected_categories,
                 size_t max_active_slots) {
    impl_->configure(expected_categories, max_active_slots);
  }

  void copy_header(std::span<uint8_t const> header) {
    impl_->copy_header(header);
  }

  // TODO: check which write_block() API is actually used

  void write_block(fragment_category cat, std::shared_ptr<block_data>&& data,
                   physical_block_cb_type physical_block_cb,
                   std::optional<std::string> meta = std::nullopt) {
    impl_->write_block(cat, std::move(data), std::move(physical_block_cb),
                       std::move(meta));
  }

  void finish_category(fragment_category cat) { impl_->finish_category(cat); }

  void write_block(fragment_category::value_type cat,
                   std::shared_ptr<block_data>&& data,
                   std::optional<std::string> meta = std::nullopt) {
    impl_->write_block(cat, std::move(data), std::move(meta));
  }

  void write_metadata_v2_schema(std::shared_ptr<block_data>&& data) {
    impl_->write_metadata_v2_schema(std::move(data));
  }

  void write_metadata_v2(std::shared_ptr<block_data>&& data) {
    impl_->write_metadata_v2(std::move(data));
  }

  void write_history(std::shared_ptr<block_data>&& data) {
    impl_->write_history(std::move(data));
  }

  void check_block_compression(
      compression_type compression, std::span<uint8_t const> data,
      std::optional<fragment_category::value_type> cat = std::nullopt) {
    impl_->check_block_compression(compression, data, cat);
  }

  void write_section(
      section_type type, compression_type compression,
      std::span<uint8_t const> data,
      std::optional<fragment_category::value_type> cat = std::nullopt) {
    impl_->write_section(type, compression, data, cat);
  }

  void write_compressed_section(fs_section sec, std::span<uint8_t const> data) {
    impl_->write_compressed_section(std::move(sec), data);
  }

  void flush() { impl_->flush(); }

  size_t size() const { return impl_->size(); }

  class impl {
   public:
    virtual ~impl() = default;

    virtual void add_default_compressor(block_compressor bc) = 0;
    virtual void add_category_compressor(fragment_category::value_type cat,
                                         block_compressor bc) = 0;
    virtual compression_constraints
    get_compression_constraints(fragment_category::value_type cat,
                                std::string const& metadata) const = 0;
    virtual block_compressor const&
    get_compressor(section_type type,
                   std::optional<fragment_category::value_type> cat) const = 0;
    virtual void
    configure(std::vector<fragment_category> const& expected_categories,
              size_t max_active_slots) = 0;
    virtual void copy_header(std::span<uint8_t const> header) = 0;
    virtual void
    write_block(fragment_category cat, std::shared_ptr<block_data>&& data,
                physical_block_cb_type physical_block_cb,
                std::optional<std::string> meta) = 0;
    virtual void finish_category(fragment_category cat) = 0;
    virtual void write_block(fragment_category::value_type cat,
                             std::shared_ptr<block_data>&& data,
                             std::optional<std::string> meta) = 0;
    virtual void
    write_metadata_v2_schema(std::shared_ptr<block_data>&& data) = 0;
    virtual void write_metadata_v2(std::shared_ptr<block_data>&& data) = 0;
    virtual void write_history(std::shared_ptr<block_data>&& data) = 0;
    virtual void check_block_compression(
        compression_type compression, std::span<uint8_t const> data,
        std::optional<fragment_category::value_type> cat) = 0;
    virtual void
    write_section(section_type type, compression_type compression,
                  std::span<uint8_t const> data,
                  std::optional<fragment_category::value_type> cat) = 0;
    virtual void
    write_compressed_section(fs_section sec, std::span<uint8_t const> data) = 0;
    virtual void flush() = 0;
    virtual size_t size() const = 0;
  };

 private:
  std::unique_ptr<impl> impl_;
};
} // namespace dwarfs
