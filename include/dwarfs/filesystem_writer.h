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

#include <ostream>
#include <vector>

#include "dwarfs/fstypes.h"
#include "dwarfs/worker_group.h"

namespace dwarfs {

class block_compressor;
class logger;
class progress;

class section {
 public:
  class impl {
   public:
    virtual ~impl() = default;

    // TODO
  };

  section(std::unique_ptr<impl>&& i);

 private:
  std::unique_ptr<impl> impl_;
};

class filesystem_writer {
 public:
  filesystem_writer(std::ostream& os, logger& lgr, worker_group& wg,
                    progress& prog, const block_compressor& bc,
                    size_t max_queue_size);

  filesystem_writer(std::ostream& os, logger& lgr, worker_group& wg,
                    progress& prog, const block_compressor& bc,
                    const block_compressor& schema_bc,
                    const block_compressor& metadata_bc, size_t max_queue_size);

  // section create_block();
  // section create_metadata();

  // void add_section(section&& section);

  void write_block(std::vector<uint8_t>&& data) {
    impl_->write_block(std::move(data));
  }

  void write_metadata(std::vector<uint8_t>&& data) {
    impl_->write_metadata(std::move(data));
  }

  void write_metadata_v2_schema(std::vector<uint8_t>&& data) {
    impl_->write_metadata_v2_schema(std::move(data));
  }

  void write_metadata_v2(std::vector<uint8_t>&& data) {
    impl_->write_metadata_v2(std::move(data));
  }

  void flush() { impl_->flush(); }

  size_t size() const { return impl_->size(); }

  class impl {
   public:
    virtual ~impl() = default;

    virtual void write_block(std::vector<uint8_t>&& data) = 0;
    virtual void write_metadata(std::vector<uint8_t>&& data) = 0;
    virtual void write_metadata_v2_schema(std::vector<uint8_t>&& data) = 0;
    virtual void write_metadata_v2(std::vector<uint8_t>&& data) = 0;
    virtual void flush() = 0;
    virtual size_t size() const = 0;
  };

 private:
  std::unique_ptr<impl> impl_;
};
} // namespace dwarfs
