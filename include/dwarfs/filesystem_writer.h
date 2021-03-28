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
#include <utility>

#include <folly/Range.h>

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
  filesystem_writer(
      std::ostream& os, logger& lgr, worker_group& wg, progress& prog,
      const block_compressor& bc,
      filesystem_writer_options const& options = filesystem_writer_options(),
      std::istream* header = nullptr);

  filesystem_writer(std::ostream& os, logger& lgr, worker_group& wg,
                    progress& prog, const block_compressor& bc,
                    const block_compressor& schema_bc,
                    const block_compressor& metadata_bc,
                    filesystem_writer_options const& options,
                    std::istream* header = nullptr);

  void copy_header(folly::ByteRange header) { impl_->copy_header(header); }

  void write_block(std::shared_ptr<block_data>&& data) {
    impl_->write_block(std::move(data));
  }

  void write_metadata_v2_schema(std::shared_ptr<block_data>&& data) {
    impl_->write_metadata_v2_schema(std::move(data));
  }

  void write_metadata_v2(std::shared_ptr<block_data>&& data) {
    impl_->write_metadata_v2(std::move(data));
  }

  void write_compressed_section(section_type type, compression_type compression,
                                folly::ByteRange data) {
    impl_->write_compressed_section(type, compression, data);
  }

  void flush() { impl_->flush(); }

  size_t size() const { return impl_->size(); }

  int queue_fill() const { return impl_->queue_fill(); }

  class impl {
   public:
    virtual ~impl() = default;

    virtual void copy_header(folly::ByteRange header) = 0;
    virtual void write_block(std::shared_ptr<block_data>&& data) = 0;
    virtual void
    write_metadata_v2_schema(std::shared_ptr<block_data>&& data) = 0;
    virtual void write_metadata_v2(std::shared_ptr<block_data>&& data) = 0;
    virtual void
    write_compressed_section(section_type type, compression_type compression,
                             folly::ByteRange data) = 0;
    virtual void flush() = 0;
    virtual size_t size() const = 0;
    virtual int queue_fill() const = 0;
  };

 private:
  std::unique_ptr<impl> impl_;
};
} // namespace dwarfs
