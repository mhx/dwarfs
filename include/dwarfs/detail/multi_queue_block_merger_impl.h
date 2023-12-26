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
#include <deque>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include <fmt/format.h>

#include <folly/Function.h>
#include <folly/String.h>
#include <folly/gen/String.h>

#include "dwarfs/block_merger.h"
#include "dwarfs/terminal.h"

namespace dwarfs::detail {

template <typename SourceT, typename BlockT, typename BlockPolicy>
class multi_queue_block_merger_impl : public block_merger_base,
                                      public block_merger<SourceT, BlockT>,
                                      private BlockPolicy {
 public:
  static constexpr bool const debug{false};

  using source_type = SourceT;
  using block_type = BlockT;
  using on_block_merged_callback_type =
      folly::Function<void(block_type&&, size_t)>;

  multi_queue_block_merger_impl(
      size_t num_active_slots, size_t max_queued_size,
      std::vector<source_type> const& sources,
      on_block_merged_callback_type&& on_block_merged_callback,
      BlockPolicy&& policy)
      : BlockPolicy{std::move(policy)}
      , max_queueable_size_{max_queued_size}
      , source_queue_{sources.begin(), sources.end()}
      , active_slots_(num_active_slots)
      , on_block_merged_callback_{std::move(on_block_merged_callback)} {
    for (size_t i = 0; i < active_slots_.size() && !source_queue_.empty();
         ++i) {
      active_slots_[i] = source_queue_.front();
      source_queue_.pop_front();
    }
  }

  multi_queue_block_merger_impl(multi_queue_block_merger_impl&&) = default;
  multi_queue_block_merger_impl&
  operator=(multi_queue_block_merger_impl&&) = default;
  multi_queue_block_merger_impl(const multi_queue_block_merger_impl&) = delete;
  multi_queue_block_merger_impl&
  operator=(const multi_queue_block_merger_impl&) = delete;

  void add(source_type src, block_type blk) override {
    auto const block_size = this->block_size(blk);

    std::unique_lock lock{mx_};

    cv_.wait(lock, [this, &src, &block_size] {
      auto queueable = this->queueable_size();

      // if this is the active slot, we can accept the block if there is
      // enough space left in the queue
      if (active_slots_[active_slot_index_] == src) {
        return block_size <= queueable;
      }

      // otherwise, we must ensure that it is always possible to accept
      // a worst case sized block
      return block_size + max_worst_case_source_block_size() <= queueable;
    });

    if (!is_valid_source(src)) {
      throw std::runtime_error{"invalid source"};
    }

    block_queues_[src].emplace_back(std::move(blk));

    if constexpr (debug) {
      dump_state(fmt::format("add({}, {})", src, block_size), termcolor::RED);
    }

    while (try_merge_block()) {
    }

    cv_.notify_all();
  }

  void finish(source_type src) override {
    std::unique_lock lock{mx_};

    block_queues_[src].emplace_back(std::nullopt);

    if constexpr (debug) {
      dump_state(fmt::format("finish({})", src), termcolor::CYAN);
    }

    while (try_merge_block()) {
    }

    cv_.notify_all();
  }

  void release(size_t amount) override {
    std::unique_lock lock{mx_};

    assert(releaseable_size_ >= amount);

    releaseable_size_ -= amount;

    if constexpr (debug) {
      dump_state(fmt::format("release({})", amount), termcolor::YELLOW);
    }

    cv_.notify_all();
  }

 private:
  size_t queueable_size() const {
    size_t total_active_size{queued_size() + releaseable_size_};
    assert(total_active_size <= max_queueable_size_);
    return max_queueable_size_ - total_active_size;
  }

  size_t queued_size() const {
    size_t size{0};
    for (auto const& bq : block_queues_) {
      for (auto const& blk : bq.second) {
        if (blk.has_value()) {
          size += this->block_size(*blk);
        }
      }
    }
    return size;
  }

  void dump_state(std::string what, termcolor color) const {
    std::cout << terminal_ansi_colored(fmt::format("**** {} ****", what), color)
              << std::endl;

    std::cout << "index: " << active_slot_index_
              << ", queueable: " << queueable_size() << "/"
              << max_queueable_size_ << ", releaseable: " << releaseable_size_
              << std::endl;

    std::cout << "active: ";
    for (size_t i = 0; i < active_slots_.size(); ++i) {
      auto const& src = active_slots_[i];
      if (src) {
        std::cout << terminal_ansi_colored(
            fmt::format("{} ", src.value()),
            i == active_slot_index_ ? termcolor::BOLD_GREEN : termcolor::GRAY);
      } else {
        std::cout << terminal_ansi_colored("- ", termcolor::GRAY);
      }
    }
    std::cout << std::endl;

    std::cout << "queued: ";
    for (auto const& src : source_queue_) {
      std::cout << src << " ";
    }
    std::cout << std::endl;

    for (auto const& [src, q] : block_queues_) {
      if (q.empty()) {
        continue;
      }

      auto const queued_sizes = folly::join(
          ", ", folly::gen::from(q) | folly::gen::map([this](auto const& blk) {
                  return blk.has_value()
                             ? std::to_string(this->block_size(*blk))
                             : "&";
                }) | folly::gen::as<std::vector<std::string>>());

      auto const text =
          fmt::format("blocks({}): {} -> {}", src, q.size(), queued_sizes);

      if (src == active_slots_[active_slot_index_]) {
        std::cout << terminal_ansi_colored(text, termcolor::BOLD_GREEN);
      } else {
        std::cout << text;
      }

      std::cout << std::endl;
    }
  }

  bool is_valid_source(source_type src) const {
    return std::find(begin(active_slots_), end(active_slots_), src) !=
               end(active_slots_) ||
           std::find(begin(source_queue_), end(source_queue_), src) !=
               end(source_queue_);
  }

  size_t max_worst_case_source_block_size() const {
    if (!cached_max_worst_case_source_block_size_) {
      size_t max_size{0};

      for (auto const& src : active_slots_) {
        if (src) {
          max_size =
              std::max(max_size, this->worst_case_source_block_size(*src));
        }
      }

      for (auto const& src : source_queue_) {
        max_size = std::max(max_size, this->worst_case_source_block_size(src));
      }

      cached_max_worst_case_source_block_size_ = max_size;
    }

    return *cached_max_worst_case_source_block_size_;
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
    it->second.pop_front();

    auto const not_last = blk.has_value();
    std::optional<size_t> block_size;

    if (not_last) {
      block_size = this->block_size(*blk);
      releaseable_size_ += *block_size;
      on_block_merged_callback_(std::move(*blk), *block_size);
    } else {
      block_queues_.erase(it);
      update_active(ix);
      cached_max_worst_case_source_block_size_.reset();
    }

    do {
      active_slot_index_ = (active_slot_index_ + 1) % active_slots_.size();
    } while (active_slot_index_ != ix && !active_slots_[active_slot_index_]);

    if constexpr (debug) {
      if (not_last) {
        dump_state(fmt::format("merge({}, {})", src, *block_size),
                   termcolor::GREEN);
      } else {
        dump_state(fmt::format("final({})", src), termcolor::BOLD_GREEN);
      }
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
  size_t const max_queueable_size_;
  size_t releaseable_size_{0};
  std::optional<size_t> mutable cached_max_worst_case_source_block_size_;
  std::unordered_map<source_type, std::deque<std::optional<block_type>>>
      block_queues_;
  std::deque<source_type> source_queue_;
  std::vector<std::optional<source_type>> active_slots_;
  on_block_merged_callback_type on_block_merged_callback_;
};

} // namespace dwarfs::detail
