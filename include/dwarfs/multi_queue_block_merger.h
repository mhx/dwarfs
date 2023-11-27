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

#include <cassert>
#include <memory>
#include <vector>

#include <folly/Function.h>

#include "dwarfs/block_merger.h"
#include "dwarfs/detail/multi_queue_block_merger_impl.h"

namespace dwarfs {

class block_merger_whole_block_policy {
 public:
  template <typename BlockT>
  static size_t block_size(BlockT const&) {
    return 1;
  }

  template <typename SourceT>
  static size_t worst_case_source_block_size(SourceT const&) {
    return 1;
  }
};

/**
 * Deterministically merge blocks from multiple sources into a single stream.
 *
 * This class implements a block merger that deterministically merges blocks
 * from multiple sources into a single stream. The order of the sources is
 * fixed and the order of the blocks within each source is preserved.
 * The number of active slots determines how many sources can be merged
 * simultaneously. The number of queued blocks determines the overall number
 * of blocks that can be queued for merging before the merger blocks.
 *
 * You'd typically set the number of active slots to the number of threads
 * that are used to produce blocks. While it is possible to use more threads
 * than active slots, this will not improve performance and will only increase
 * the memory footprint. However, it is not possible to use less threads than
 * active slots, as this will cause the merger to ultimately block all threads
 * and deadlock, since it is assuming that another thread will eventually add
 * more blocks.
 *
 * The order of the blocks in the output stream is only determined by the order
 * of the sources and the number of active slots. The number of queued blocks
 * only has an effect on the efficiency of the merger. Being able to queue more
 * blocks means that the merger will block less often, but it also means that
 * more memory is used.
 *
 * It is vital that the blocks passed via add() are generated in the correct
 * order as specified by the sources vector.
 *
 * The callback is called for each merged block while the merger's internal
 * mutex is locked. This means that the callback should not block for a long
 * time. The callback is called from the thread that calls the add() method.
 *
 * The merged_block_holder class is used to hold a merged block. As long as
 * the holder is alive, the held block will count towards the number of
 * queued blocks. Once the holder is destroyed, the held block will be
 * released and the number of queued blocks will be decremented.
 *
 * It is also possible to provide a policy that returns the size of a block
 * as well as the worst case size for a block from a certain source. This
 * can be useful to keep an upper bound on the memory usage of the merger.
 * It is even possible to only partially release a block, e.g. after the
 * block has been compressed.
 *
 * TODO: The lifetime of block holders should be extended to before adding
 *       a block to the merger. This would allow the merger to block even
 *       before a block is added (which already consumes resources at that
 *       point). It would also allow blocks to change size while being
 *       processed by the merger.
 */
template <typename SourceT, typename BlockT,
          typename BlockPolicy = block_merger_whole_block_policy>
class multi_queue_block_merger : public block_merger<SourceT, BlockT> {
 public:
  using source_type = SourceT;
  using block_type = BlockT;
  using block_holder_type = merged_block_holder<block_type>;
  using on_block_merged_callback_type =
      folly::Function<void(block_holder_type)>;

  multi_queue_block_merger() = default;

  multi_queue_block_merger(
      size_t num_active_slots, size_t max_queued_blocks,
      std::vector<source_type> const& sources,
      on_block_merged_callback_type&& on_block_merged_callback,
      BlockPolicy&& policy = block_merger_whole_block_policy{})
      : state_{std::make_unique<state>(
            num_active_slots, max_queued_blocks, sources,
            std::move(on_block_merged_callback), std::move(policy))} {}

  void add(source_type src, block_type blk) override {
    assert(state_);
    state_->impl->add(std::move(src), std::move(blk));
  }

  void finish(source_type src) override {
    assert(state_);
    state_->impl->finish(std::move(src));
  }

 private:
  using impl_type =
      detail::multi_queue_block_merger_impl<SourceT, BlockT, BlockPolicy>;

  struct state {
    state(size_t num_active_slots, size_t max_queued_blocks,
          std::vector<source_type> const& sources,
          on_block_merged_callback_type&& on_block_merged_callback,
          BlockPolicy&& policy = block_merger_whole_block_policy{})
        : callback{std::move(on_block_merged_callback)}
        , impl{std::make_shared<impl_type>(
              num_active_slots, max_queued_blocks, sources,
              [this](block_type&& blk, size_t size) {
                on_block_merged(std::move(blk), size);
              },
              std::move(policy))} {}

    void on_block_merged(block_type&& blk, size_t size) {
      callback(block_holder_type{std::move(blk), size, impl});
    }

    on_block_merged_callback_type callback;
    std::shared_ptr<impl_type> impl;
  };

  std::unique_ptr<state> state_;
};

} // namespace dwarfs
