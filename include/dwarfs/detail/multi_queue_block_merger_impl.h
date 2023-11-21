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
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include <fmt/format.h>

#include "dwarfs/block_merger.h"

namespace dwarfs::detail {

/**
 * TODO: Support different policies for how much data can be queued.
 *       The current behavior is to limit the total number of blocks that
 *       can be queued. This is not ideal for sources that produce blocks
 *       of different sizes, or when blocks that are still held without
 *       being released change their size (because they have been compressed).
 *
 *       You can then release a *size* instead of a *block*, and each source
 *       is assigned a worst-case size that is used to determine if a new
 *       block can be queued or not (via the source_distance() function).
 */

template <typename SourceT, typename BlockT>
class multi_queue_block_merger_impl : public block_merger_base,
                                      public block_merger<SourceT, BlockT> {
 public:
  static constexpr bool const debug{false};

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

  void add(source_type src, block_type blk) override {
    std::unique_lock lock{mx_};

    cv_.wait(lock,
             [this, &src] { return source_distance(src) < num_queueable_; });

    --num_queueable_;

    if (!is_valid_source(src)) {
      throw std::runtime_error{"invalid source"};
    }

    block_queues_[src].emplace(std::move(blk));

    if constexpr (debug) {
      dump_state(fmt::format("add({})", src));
    }

    while (try_merge_block()) {
    }

    cv_.notify_all();
  }

  void finish(source_type src) override {
    std::unique_lock lock{mx_};

    block_queues_[src].emplace(std::nullopt);

    if constexpr (debug) {
      dump_state(fmt::format("finish({})", src));
    }

    while (try_merge_block()) {
    }

    cv_.notify_all();
  }

  void release() override {
    std::unique_lock lock{mx_};

    assert(num_releaseable_ > 0);

    --num_releaseable_;
    ++num_queueable_;

    if constexpr (debug) {
      dump_state("release");
    }

    cv_.notify_all();
  }

 private:
  void dump_state(std::string what) const {
    std::cout << "**** " << what << " ****" << std::endl;

    std::cout << "index: " << active_slot_index_
              << ", queueable: " << num_queueable_
              << ", releaseable: " << num_releaseable_ << std::endl;

    std::cout << "active: ";
    for (auto const& src : active_slots_) {
      if (src) {
        std::cout << src.value() << " ";
      } else {
        std::cout << "- ";
      }
    }
    std::cout << std::endl;

    std::cout << "queue: ";
    for (auto const& src : source_queue_) {
      std::cout << src << " ";
    }
    std::cout << std::endl;

    std::cout << "blocks: ";
    for (auto const& [src, q] : block_queues_) {
      std::cout << src << "(" << q.size() << ") ";
    }
    std::cout << std::endl;
  }

  bool is_valid_source(source_type src) const {
    return std::find(begin(active_slots_), end(active_slots_), src) !=
               end(active_slots_) ||
           std::find(begin(source_queue_), end(source_queue_), src) !=
               end(source_queue_);
  }

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

    if constexpr (debug) {
      std::cout << "distance(" << src << "): " << distance << std::endl;
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

    auto blk = std::move(it->second.front());
    it->second.pop();

    const bool not_last = blk.has_value();

    if (not_last) {
      ++num_releaseable_;
      on_block_merged_callback_(std::move(*blk));
    } else {
      block_queues_.erase(it);
      update_active(ix);
    }

    do {
      active_slot_index_ = (active_slot_index_ + 1) % active_slots_.size();
    } while (active_slot_index_ != ix && !active_slots_[active_slot_index_]);

    if constexpr (debug) {
      dump_state(not_last ? fmt::format("merge({})", src)
                          : fmt::format("final({})", src));
    }

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
  std::unordered_map<source_type, std::queue<std::optional<block_type>>>
      block_queues_;
  std::deque<source_type> source_queue_;
  std::vector<std::optional<source_type>> active_slots_;
  on_block_merged_callback_type on_block_merged_callback_;
};

} // namespace dwarfs::detail
