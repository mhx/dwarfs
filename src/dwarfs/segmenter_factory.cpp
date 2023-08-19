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

#include "dwarfs/segmenter_factory.h"
#include "dwarfs/categorizer.h"

namespace dwarfs {

class segmenter_factory_ final : public segmenter_factory::impl {
 public:
  segmenter_factory_(logger& lgr, progress& prog,
                     std::shared_ptr<categorizer_manager> catmgr,
                     const segmenter_factory::config& cfg)
      : lgr_{lgr}
      , prog_{prog}
      , catmgr_{catmgr}
      , cfg_{cfg} {}

  segmenter create(fragment_category cat, size_t cat_size,
                   compression_constraints const& cc,
                   std::shared_ptr<block_manager> blkmgr,
                   segmenter::block_ready_cb block_ready) const override {
    segmenter::config cfg;

    if (catmgr_) {
      cfg.context = category_prefix(catmgr_, cat);
    }

    cfg.blockhash_window_size = cfg_.blockhash_window_size.get(cat);
    cfg.window_increment_shift = cfg_.window_increment_shift.get(cat);
    cfg.max_active_blocks = cfg_.max_active_blocks.get(cat);
    cfg.bloom_filter_size = cfg_.bloom_filter_size.get(cat);
    cfg.block_size_bits = cfg_.block_size_bits;

    return segmenter(lgr_, prog_, std::move(blkmgr), cfg, cc, cat_size,
                     std::move(block_ready));
  }

  size_t get_block_size() const override {
    return static_cast<size_t>(1) << cfg_.block_size_bits;
  }

 private:
  logger& lgr_;
  progress& prog_;
  std::shared_ptr<categorizer_manager> catmgr_;
  segmenter_factory::config cfg_;
};

segmenter_factory::segmenter_factory(
    logger& lgr, progress& prog, std::shared_ptr<categorizer_manager> catmgr,
    config const& cfg)
    : impl_(std::make_unique<segmenter_factory_>(lgr, prog, std::move(catmgr),
                                                 cfg)) {}

segmenter_factory::segmenter_factory(logger& lgr, progress& prog,
                                     config const& cfg)
    : segmenter_factory(lgr, prog, nullptr, cfg) {}

} // namespace dwarfs
