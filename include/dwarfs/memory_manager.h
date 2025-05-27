/* vim:set ts=2 sw=2 sts=2 et: */
/**
 * \author     Marcus Holland-Moritz (github@mhxnet.de)
 * \copyright  Copyright (c) Marcus Holland-Moritz
 *
 * This file is part of dwarfs.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the “Software”), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <algorithm>
#include <atomic>
#include <latch>
#include <memory>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <fmt/format.h>

#include <dwarfs/error.h>
#include <dwarfs/util.h>

namespace dwarfs {

class memory_manager : public std::enable_shared_from_this<memory_manager> {
  friend class memory_request;

  struct memory_request {
    memory_request(std::shared_ptr<memory_manager> mgr, size_t size,
                   size_t sequence, int priority, std::string_view tag)
        : mgr{std::move(mgr)}
        , size{size}
        , sequence{sequence}
        , priority{priority}
        , tag{tag} {}

    ~memory_request() { release(); }

    bool hipri() const { return priority < 0; }

    void release() {
      if (mgr) {
        mgr->release(size, sequence);
        mgr.reset();
      }
    }

    bool release_partial(size_t released_size) {
      if (mgr) {
        if (released_size < size) {
          size -= released_size;
          mgr->release_partial(released_size, sequence);
          return true; // Still active
        }

        // Fully released
        mgr->release(size, sequence);
        mgr.reset();
      }

      return false;
    }

    std::shared_ptr<memory_manager> mgr;
    size_t size;
    size_t sequence;
    int priority;
    std::string_view tag;
    std::latch latch{1};
  };

  using request_ptr = std::shared_ptr<memory_request>;

 public:
  class credit_handle {
   public:
    credit_handle() = default;
    explicit credit_handle(request_ptr req)
        : req_{std::move(req)} {}

    void wait() { req_->latch.wait(); }

    void release() { req_.reset(); }

    void release_partial(size_t size) {
      if (size > 0 && req_ && !req_->release_partial(size)) {
        req_.reset();
      }
    }

    explicit operator bool() const { return static_cast<bool>(req_); }

   private:
    request_ptr req_;
  };

  explicit memory_manager(size_t limit, size_t hipri_reserve = 0)
      : limit_{limit}
      , hipri_reserve_{hipri_reserve} {}

  credit_handle
  request_noblock(size_t size, int priority, std::string_view tag = "") {
    std::unique_lock lock(mutex_);
    if (size == 0 || size > limit_) {
      DWARFS_THROW(runtime_error,
                   fmt::format("Invalid memory request size: {} (limit: {})",
                               size, limit_));
    }
    auto req = std::make_shared<memory_request>(shared_from_this(), size,
                                                sequence_++, priority, tag);
    pending_.emplace_back(req);
    std::ranges::push_heap(pending_, req_compare{});
    fulfill_and_unlock(lock);
    return credit_handle(std::move(req));
  }

  credit_handle request(size_t size, int priority, std::string_view tag = "") {
    auto hdl = request_noblock(size, priority, tag);
    hdl.wait();
    return hdl;
  }

  std::string status() const {
    auto const requests = get_request_info();

    std::vector<std::string_view> tags(requests.size());
    std::ranges::transform(requests, tags.begin(),
                           [](auto const& pair) { return pair.first; });
    std::ranges::sort(tags);

    std::string result =
        fmt::format("{}/{}", size_with_unit(used_), size_with_unit(limit_));

    for (auto const& tag : tags) {
      auto const& info = requests.at(tag);
      result +=
          fmt::format("; {}: {} ({}) A, {} ({}) P", tag,
                      size_with_unit(info.active_size), info.active_count,
                      size_with_unit(info.pending_size), info.pending_count);
    }

    return result;
  }

  struct usage_info {
    std::string_view tag;
    size_t size;
  };

  std::vector<usage_info> get_usage_info() const {
    auto const requests = get_request_info();
    std::vector<usage_info> usage;
    size_t total_used{0};
    for (auto const& [tag, info] : requests) {
      total_used += info.active_size;
      usage.emplace_back(usage_info{tag, info.active_size});
    }
    DWARFS_CHECK(total_used <= limit_,
                 fmt::format("Total used memory exceeds limit: {} > {}",
                             total_used, limit_));
    usage.emplace_back(usage_info{"free", limit_ - total_used});
    return usage;
  }

 private:
  struct request_info {
    size_t active_size{0};
    size_t active_count{0};
    size_t pending_size{0};
    size_t pending_count{0};
  };

  std::unordered_map<std::string_view, request_info> get_request_info() const {
    std::unordered_map<std::string_view, request_info> requests;

    std::lock_guard lock(mutex_);

    for (auto const& [_, info] : active_) {
      auto& entry = requests[info.tag];
      entry.active_size += info.size;
      ++entry.active_count;
    }

    for (auto const& req : pending_) {
      auto& entry = requests[req->tag];
      entry.pending_size += req->size;
      ++entry.pending_count;
    }

    return requests;
  }

  void fulfill_and_unlock(std::unique_lock<std::mutex>& lock) {
    std::vector<request_ptr> granted;

    while (!pending_.empty()) {
      auto const& req = pending_.front();
      auto need = req->size;

      if (used_ + need > limit_) {
        // Cannot fulfill this request now
        break;
      }

      if (!req->hipri() && used_ + need > lopri_limit()) {
        // Cannot fulfill low priority request without exceeding hipri reserve
        break;
      }

      // Fulfill the request
      used_ += need;
      active_.emplace(req->sequence,
                      active_info{need, req->priority, req->tag});
      granted.emplace_back(std::move(req));
      std::ranges::pop_heap(pending_, req_compare{});
      pending_.pop_back();
    }

    lock.unlock();

    for (auto const& req : granted) {
      req->latch.count_down();
    }
  }

  void release(size_t size, size_t sequence) {
    std::unique_lock lock(mutex_);

    if (active_.erase(sequence)) {
      used_ -= size;
      fulfill_and_unlock(lock);
    }
  }

  void release_partial(size_t released_size, size_t sequence) {
    std::unique_lock lock(mutex_);

    if (auto it = active_.find(sequence); it != active_.end()) {
      it->second.size -= released_size;
      used_ -= released_size;
      fulfill_and_unlock(lock);
    }
  }

  size_t lopri_limit() const { return limit_ - hipri_reserve_.load(); }

  struct req_compare {
    bool operator()(request_ptr const& lhs, request_ptr const& rhs) {
      return lhs->priority > rhs->priority ||
             (lhs->priority == rhs->priority && lhs->sequence > rhs->sequence);
    }
  };

  struct active_info {
    size_t size;
    int priority;
    std::string_view tag;
  };

  std::mutex mutable mutex_;
  std::vector<request_ptr> pending_;
  std::unordered_map<size_t, active_info> active_;
  size_t sequence_{0};
  size_t used_{};
  size_t limit_{};
  std::atomic<size_t> hipri_reserve_;
};

} // namespace dwarfs
