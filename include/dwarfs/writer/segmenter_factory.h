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

#include <memory>

#include <dwarfs/types.h>
#include <dwarfs/writer/categorized_option.h>
#include <dwarfs/writer/segmenter.h>

namespace dwarfs {

class logger;

struct compression_constraints;

namespace writer {

class categorizer_manager;
class writer_progress;

class segmenter_factory {
 public:
  struct config {
    categorized_option<unsigned> blockhash_window_size;
    categorized_option<unsigned> window_increment_shift;
    categorized_option<size_t> max_active_blocks;
    categorized_option<unsigned> bloom_filter_size;
    unsigned block_size_bits{22};
    bool enable_sparse_files{false};
  };

  segmenter_factory(logger& lgr, writer_progress& prog);
  segmenter_factory(logger& lgr, writer_progress& prog, config const& cfg);
  segmenter_factory(logger& lgr, writer_progress& prog,
                    std::shared_ptr<categorizer_manager> catmgr);
  segmenter_factory(logger& lgr, writer_progress& prog,
                    std::shared_ptr<categorizer_manager> catmgr,
                    config const& cfg);

  segmenter create(fragment_category cat, file_size_t cat_size,
                   compression_constraints const& cc,
                   std::shared_ptr<internal::block_manager> blkmgr,
                   segmenter::block_ready_cb block_ready) const {
    return impl_->create(cat, cat_size, cc, std::move(blkmgr),
                         std::move(block_ready));
  }

  size_t get_block_size() const { return impl_->get_block_size(); }

  uint64_t estimate_memory_usage(fragment_category cat,
                                 compression_constraints const& cc) const {
    return impl_->estimate_memory_usage(cat, cc);
  }

  class impl {
   public:
    virtual ~impl() = default;

    virtual segmenter create(fragment_category cat, file_size_t cat_size,
                             compression_constraints const& cc,
                             std::shared_ptr<internal::block_manager> blkmgr,
                             segmenter::block_ready_cb block_ready) const = 0;
    virtual size_t get_block_size() const = 0;
    virtual uint64_t
    estimate_memory_usage(fragment_category cat,
                          compression_constraints const& cc) const = 0;
  };

 private:
  std::unique_ptr<impl> impl_;
};

} // namespace writer

} // namespace dwarfs
