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

#include <algorithm>
#include <cassert>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <queue>
#include <unordered_map>
#include <vector>

#include "dwarfs/block_merger.h"

namespace dwarfs::detail {

template <typename SourceT, typename BlockT>
class multi_queue_block_merger_impl : public block_merger_base,
                                      public block_merger<SourceT, BlockT> {
 public:
  using source_type = SourceT;
  using block_type = BlockT;
  using on_block_merged_callback_type = std::function<void(block_type&&)>;

  multi_queue_block_merger_impl(
      size_t num_active_slots, size_t max_queued_blocks,
      std::vector<source_type> const& sources,
      on_block_merged_callback_type on_block_merged_callback)
      : num_queueable_{max_queued_blocks}
      , source_queue_{sources.begin(), sources.end()}
      , active_slots_(num_active_slots)
      , on_block_merged_callback_{on_block_merged_callback} {
    for (size_t i = 0; i < active_slots_.size() && !source_queue_.empty();
         ++i) {
      active_slots_[i] = source_queue_.front();
      source_queue_.pop_front();
    }
  }

  void add(source_type src, block_type blk, bool is_last) override {
    std::unique_lock lock{mx_};

    cv_.wait(lock,
             [this, &src] { return source_distance(src) < num_queueable_; });

    --num_queueable_;

    block_queues_[src].emplace(std::move(blk), is_last);

    while (try_merge_block()) {
    }

    cv_.notify_all();
  }

  void release() override {
    std::unique_lock lock{mx_};

    assert(num_releaseable_ > 0);

    --num_releaseable_;
    ++num_queueable_;

    cv_.notify_all();
  }

 private:
  size_t source_distance(source_type src) const {
    auto ix = active_slot_index_;
    size_t distance{0};

    while (active_slots_[ix] && active_slots_[ix].value() != src) {
      ++distance;

      do {
        ix = (ix + 1) % active_slots_.size();
      } while (ix != active_slot_index_ && !active_slots_[ix]);

      if (ix == active_slot_index_) {
        auto it = std::find(begin(source_queue_), end(source_queue_), src);
        distance += std::distance(begin(source_queue_), it);
        break;
      }
    }

    return distance;
  }

  bool try_merge_block() {
    auto const ix = active_slot_index_;

    assert(active_slots_[ix]);

    auto src = active_slots_[ix].value();
    auto it = block_queues_.find(src);

    if (it == block_queues_.end() || it->second.empty()) {
      return false;
    }

    auto [blk, is_last] = std::move(it->second.front());
    it->second.pop();

    ++num_releaseable_;
    on_block_merged_callback_(std::move(blk));

    if (is_last) {
      block_queues_.erase(it);
      update_active(ix);
    }

    do {
      active_slot_index_ = (active_slot_index_ + 1) % active_slots_.size();
    } while (active_slot_index_ != ix && !active_slots_[active_slot_index_]);

    return active_slot_index_ != ix || active_slots_[active_slot_index_];
  }

  void update_active(size_t ix) {
    if (!source_queue_.empty()) {
      active_slots_[ix] = source_queue_.front();
      source_queue_.pop_front();
    } else {
      active_slots_[ix].reset();
    }
  }

  std::recursive_mutex mx_;
  std::condition_variable_any cv_;
  size_t active_slot_index_{0};
  size_t num_queueable_;
  size_t num_releaseable_{0};
  std::unordered_map<source_type, std::queue<std::pair<block_type, bool>>>
      block_queues_;
  std::deque<source_type> source_queue_;
  std::vector<std::optional<source_type>> active_slots_;
  on_block_merged_callback_type on_block_merged_callback_;
};

} // namespace dwarfs::detail
