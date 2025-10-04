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
#include <functional>
#include <memory>
#include <vector>

#include <dwarfs/byte_buffer.h>
#include <dwarfs/types.h>

namespace dwarfs {

struct compression_constraints;

class logger;

namespace writer {

class writer_progress;

namespace internal {

class block_manager;
class chunkable;

} // namespace internal

class segmenter {
 public:
  struct config {
    std::string context{};
    unsigned blockhash_window_size{12};
    unsigned window_increment_shift{1};
    size_t max_active_blocks{1};
    unsigned bloom_filter_size{4};
    unsigned block_size_bits{22};
    bool enable_sparse_files{false};
  };

  using block_ready_cb =
      std::function<void(shared_byte_buffer, size_t logical_block_num)>;

  static uint64_t
  estimate_memory_usage(config const& cfg, compression_constraints const& cc);

  segmenter(logger& lgr, writer_progress& prog,
            std::shared_ptr<internal::block_manager> blkmgr, config const& cfg,
            compression_constraints const& cc, file_size_t total_size,
            block_ready_cb block_ready);

  void add_chunkable(internal::chunkable& chkable) {
    impl_->add_chunkable(chkable);
  }

  void finish() { impl_->finish(); }

  class impl {
   public:
    virtual ~impl() = default;

    virtual void add_chunkable(internal::chunkable& chkable) = 0;
    virtual void finish() = 0;
  };

 private:
  std::unique_ptr<impl> impl_;
};

} // namespace writer

} // namespace dwarfs
