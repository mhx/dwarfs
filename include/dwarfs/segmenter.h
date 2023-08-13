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
#include <memory>
#include <vector>

#include <folly/Function.h>

namespace dwarfs {

class block_data;
class block_manager;
class chunkable;
class logger;
class progress;

class segmenter {
 public:
  struct config {
    unsigned blockhash_window_size;
    unsigned window_increment_shift{1};
    size_t max_active_blocks{1};
    size_t memory_limit{256 << 20};
    unsigned block_size_bits{22};
    unsigned bloom_filter_size{4};
  };

  using block_ready_cb = folly::Function<size_t(std::shared_ptr<block_data>)>;

  segmenter(logger& lgr, progress& prog, std::shared_ptr<block_manager> blkmgr,
            const config& cfg, block_ready_cb block_ready);

  void add_chunkable(chunkable& chkable) { impl_->add_chunkable(chkable); }

  void finish() { impl_->finish(); }

  class impl {
   public:
    virtual ~impl() = default;

    virtual void add_chunkable(chunkable& chkable) = 0;
    virtual void finish() = 0;
  };

 private:
  std::unique_ptr<impl> impl_;
};
} // namespace dwarfs
