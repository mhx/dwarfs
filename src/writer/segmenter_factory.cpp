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

#include <dwarfs/writer/categorizer.h>
#include <dwarfs/writer/segmenter_factory.h>

namespace dwarfs::writer {

namespace internal {

class segmenter_factory_ final : public segmenter_factory::impl {
 public:
  segmenter_factory_(logger& lgr, writer_progress& prog,
                     std::shared_ptr<categorizer_manager> catmgr,
                     segmenter_factory::config const& cfg)
      : lgr_{lgr}
      , prog_{prog}
      , catmgr_{std::move(catmgr)}
      , cfg_{cfg} {}

  segmenter create(fragment_category cat, file_size_t cat_size,
                   compression_constraints const& cc,
                   std::shared_ptr<block_manager> blkmgr,
                   segmenter::block_ready_cb block_ready) const override {
    return {lgr_, prog_,    std::move(blkmgr),     make_segmenter_config(cat),
            cc,   cat_size, std::move(block_ready)};
  }

  size_t get_block_size() const override {
    return static_cast<size_t>(1) << cfg_.block_size_bits;
  }

  uint64_t
  estimate_memory_usage(fragment_category cat,
                        compression_constraints const& cc) const override {
    return segmenter::estimate_memory_usage(make_segmenter_config(cat), cc);
  }

 private:
  segmenter::config make_segmenter_config(fragment_category cat) const {
    segmenter::config cfg;

    if (catmgr_) {
      cfg.context = category_prefix(catmgr_, cat);
    }

    cfg.blockhash_window_size = cfg_.blockhash_window_size.get(cat);
    cfg.window_increment_shift = cfg_.window_increment_shift.get(cat);
    cfg.max_active_blocks = cfg_.max_active_blocks.get(cat);
    cfg.bloom_filter_size = cfg_.bloom_filter_size.get(cat);
    cfg.block_size_bits = cfg_.block_size_bits;
    cfg.enable_sparse_files = cfg_.enable_sparse_files;

    return cfg;
  }

  logger& lgr_;
  writer_progress& prog_;
  std::shared_ptr<categorizer_manager> catmgr_;
  segmenter_factory::config cfg_;
};

} // namespace internal

segmenter_factory::segmenter_factory(
    logger& lgr, writer_progress& prog,
    std::shared_ptr<categorizer_manager> catmgr, config const& cfg)
    : impl_(std::make_unique<internal::segmenter_factory_>(
          lgr, prog, std::move(catmgr), cfg)) {}

segmenter_factory::segmenter_factory(logger& lgr, writer_progress& prog,
                                     config const& cfg)
    : segmenter_factory(lgr, prog, nullptr, cfg) {}

segmenter_factory::segmenter_factory(logger& lgr, writer_progress& prog)
    : segmenter_factory(lgr, prog, config{}) {}

segmenter_factory::segmenter_factory(
    logger& lgr, writer_progress& prog,
    std::shared_ptr<categorizer_manager> catmgr)
    : segmenter_factory(lgr, prog, std::move(catmgr), config{}) {}

} // namespace dwarfs::writer
