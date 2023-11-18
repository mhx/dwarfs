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

namespace dwarfs {

template <typename SourceT, typename BlockT>
class multi_queue_block_merger : public block_merger<SourceT, BlockT> {
 public:
  using source_type = SourceT;
  using block_type = BlockT;

  multi_queue_block_merger(size_t num_active_slots, size_t max_queued_blocks,
                           std::vector<source_type> const& sources,
                           std::function<void(block_type)> on_block_merged)
      : num_queueable_{max_queued_blocks}
      , sources_{sources.begin(), sources.end()}
      , active_(num_active_slots)
      , on_block_merged_{on_block_merged} {
    for (size_t i = 0; i < active_.size() && !sources_.empty(); ++i) {
      active_[i] = sources_.front();
      sources_.pop_front();
    }
  }

  void add(source_type src, block_type blk, bool is_last) override {
    std::unique_lock lock{mx_};

    cv_.wait(lock,
             [this, &src] { return source_distance(src) < num_queueable_; });

    --num_queueable_;

    queues_[src].emplace(std::move(blk), is_last);

    while (try_merge_block()) {
    }

    cv_.notify_all();
  }

 private:
  size_t source_distance(source_type src) const {
    auto ix = active_index_;
    size_t distance{0};

    while (active_[ix] && active_[ix].value() != src) {
      ++distance;
      ix = (ix + 1) % active_.size();

      if (ix == active_index_) {
        auto it = std::find(begin(sources_), end(sources_), src);
        distance += std::distance(begin(sources_), it);
        break;
      }
    }

    return distance;
  }

  bool try_merge_block() {
    auto const ix = active_index_;

    assert(active_[ix]);

    auto src = active_[ix].value();
    auto it = queues_.find(src);

    if (it == queues_.end() || it->second.empty()) {
      return false;
    }

    auto [blk, is_last] = std::move(it->second.front());
    it->second.pop();

    on_block_merged_(std::move(blk));

    ++num_queueable_;

    if (is_last) {
      queues_.erase(it);
      update_active(ix);
    }

    do {
      active_index_ = (active_index_ + 1) % active_.size();
    } while (active_index_ != ix && !active_[active_index_]);

    return active_index_ != ix || active_[active_index_];
  }

  void update_active(size_t ix) {
    if (!sources_.empty()) {
      active_[ix] = sources_.front();
      sources_.pop_front();
    } else {
      active_[ix].reset();
    }
  }

  std::mutex mx_;
  std::condition_variable cv_;
  size_t active_index_{0};
  size_t num_queueable_;
  std::unordered_map<source_type, std::queue<std::pair<block_type, bool>>>
      queues_;
  std::deque<source_type> sources_;
  std::vector<std::optional<source_type>> active_;
  std::function<void(block_type)> on_block_merged_;
};

} // namespace dwarfs
