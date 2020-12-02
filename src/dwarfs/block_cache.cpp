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

#include <algorithm>
#include <atomic>
#include <cassert>
#include <deque>
#include <exception>
#include <future>
#include <iterator>
#include <mutex>
#include <new>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include <folly/container/EvictingCacheMap.h>

#include "dwarfs/block_cache.h"
#include "dwarfs/fstypes.h"
#include "dwarfs/logger.h"
#include "dwarfs/mmif.h"
#include "dwarfs/options.h"
#include "dwarfs/worker_group.h"

namespace dwarfs {

struct block {
  block(compression_type compression, off_t offset, size_t size)
      : compression(compression)
      , offset(offset)
      , size(size) {}

  const compression_type compression;
  const off_t offset;
  const size_t size;
};

class cached_block {
 public:
  cached_block(logger& lgr, block const& b, std::shared_ptr<mmif> mm,
               bool release)
      : decompressor_(std::make_unique<block_decompressor>(
            b.compression, mm->as<uint8_t>(b.offset), b.size, data_))
      , mm_(std::move(mm))
      , spec_(b)
      , log_(lgr)
      , release_(release) {}

  ~cached_block() {
    if (decompressor_) {
      try_release();
    }
  }

  // once the block is fully decompressed, we can reset the decompressor_

  // This can be called from any thread
  size_t range_end() const { return range_end_.load(); }

  const uint8_t* data() const { return data_.data(); }

  void decompress_until(size_t end) {
    while (data_.size() < end) {
      assert(decompressor_);

      if (decompressor_->decompress_frame()) {
        // We're done, free the memory
        decompressor_.reset();

        // And release the memory from the mapping
        try_release();
      }

      range_end_ = data_.size();
    }
  }

  size_t uncompressed_size() const {
    return decompressor_ ? decompressor_->uncompressed_size()
                         : range_end_.load();
  }

 private:
  void try_release() {
    if (release_) {
      if (auto ec = mm_->release(spec_.offset, spec_.size)) {
        log_.info() << "madvise() failed: " << ec.message();
      }
    }
  }

  std::atomic<size_t> range_end_{0};
  std::vector<uint8_t> data_;
  std::unique_ptr<block_decompressor> decompressor_;
  std::shared_ptr<mmif> mm_;
  block const& spec_;
  log_proxy<debug_logger_policy> log_;
  bool const release_;
};

class block_request {
 public:
  block_request() = default;

  block_request(size_t begin, size_t end, std::promise<block_range>&& promise)
      : begin_(begin)
      , end_(end)
      , promise_(std::move(promise)) {
    assert(begin_ < end_);
  }

  block_request(block_request&&) = default;
  block_request& operator=(block_request&&) = default;

  bool operator<(const block_request& rhs) const { return end_ < rhs.end_; }

  size_t end() const { return end_; }

  void fulfill(std::shared_ptr<cached_block const> block) {
    promise_.set_value(block_range(std::move(block), begin_, end_ - begin_));
  }

  void error(std::exception_ptr error) { promise_.set_exception(error); }

 private:
  size_t begin_{0};
  size_t end_{0};
  std::promise<block_range> promise_;
};

class block_request_set {
 public:
  block_request_set(std::shared_ptr<cached_block> block, size_t block_no)
      : range_end_(0)
      , block_(std::move(block))
      , block_no_(block_no) {}

  ~block_request_set() { assert(queue_.empty()); }

  size_t range_end() const { return range_end_; }

  void add(size_t begin, size_t end, std::promise<block_range>&& promise) {
    if (end > range_end_) {
      range_end_ = end;
    }

    queue_.emplace_back(begin, end, std::move(promise));
    std::push_heap(queue_.begin(), queue_.end());
  }

  void merge(block_request_set&& other) {
    queue_.reserve(queue_.size() + other.queue_.size());
    std::move(other.queue_.begin(), other.queue_.end(),
              std::back_inserter(queue_));
    other.queue_.clear();
    std::make_heap(queue_.begin(), queue_.end());
    range_end_ = std::max(range_end_, other.range_end_);
  }

  block_request get() {
    std::pop_heap(queue_.begin(), queue_.end());
    block_request tmp = std::move(queue_.back());
    queue_.pop_back();
    return tmp;
  }

  bool empty() const { return queue_.empty(); }

  size_t size() const { return queue_.size(); }

  std::shared_ptr<cached_block> block() const { return block_; }

  size_t block_no() const { return block_no_; }

 private:
  std::vector<block_request> queue_;
  size_t range_end_;
  std::shared_ptr<cached_block> block_;
  const size_t block_no_;
};

// multi-threaded block cache
template <typename LoggerPolicy>
class block_cache_ : public block_cache::impl {
 public:
  block_cache_(logger& lgr, std::shared_ptr<mmif> mm,
               block_cache_options const& options)
      : cache_(0)
      , wg_("blkcache", std::max(options.num_workers > 0
                                     ? options.num_workers
                                     : std::thread::hardware_concurrency(),
                                 static_cast<size_t>(1)))
      , mm_(std::move(mm))
      , log_(lgr)
      , options_(options) {}

  ~block_cache_() noexcept override {
    log_.debug() << "stopping cache workers";
    wg_.stop();

    if (!blocks_created_.load()) {
      return;
    }

    log_.debug() << "cached blocks:";

    for (const auto& cb : cache_) {
      log_.debug() << "  block " << cb.first << ", decompression ratio = "
                   << double(cb.second->range_end()) /
                          double(cb.second->uncompressed_size());
      update_block_stats(*cb.second);
    }

    double fast_hit_rate =
        100.0 * (active_hits_fast_ + cache_hits_fast_) / range_requests_;
    double slow_hit_rate =
        100.0 * (active_hits_slow_ + cache_hits_slow_) / range_requests_;
    double miss_rate = 100.0 - (fast_hit_rate + slow_hit_rate);
    double avg_decompression =
        100.0 * total_decompressed_bytes_ / total_block_bytes_;

    log_.info() << "blocks created: " << blocks_created_.load();
    log_.info() << "blocks evicted: " << blocks_evicted_.load();
    log_.info() << "request sets merged: " << sets_merged_.load();
    log_.info() << "total requests: " << range_requests_.load();
    log_.info() << "active hits (fast): " << active_hits_fast_.load();
    log_.info() << "active hits (slow): " << active_hits_slow_.load();
    log_.info() << "cache hits (fast): " << cache_hits_fast_.load();
    log_.info() << "cache hits (slow): " << cache_hits_slow_.load();

    log_.info() << "total bytes decompressed: " << total_decompressed_bytes_;
    log_.info() << "average block decompression: "
                << fmt::format("{:.1f}", avg_decompression) << "%";

    log_.info() << "fast hit rate: " << fmt::format("{:.3f}", fast_hit_rate)
                << "%";
    log_.info() << "slow hit rate: " << fmt::format("{:.3f}", slow_hit_rate)
                << "%";
    log_.info() << "miss rate: " << fmt::format("{:.3f}", miss_rate) << "%";
  }

  size_t block_count() const override { return block_.size(); }

  void insert(compression_type comp, off_t offset, size_t size) override {
    block_.emplace_back(comp, offset, size);
  }

  void set_block_size(size_t size) override {
    // XXX: This currently inevitably clears the cache
    auto max_blocks = std::max<size_t>(options_.max_bytes / size, 1);
    {
      std::lock_guard<std::mutex> lock(mx_);
      cache_.~lru_type();
      new (&cache_) lru_type(max_blocks);
      cache_.setPruneHook(
          [this](size_t block_no, std::shared_ptr<cached_block>&& block) {
            log_.debug() << "evicting block " << block_no
                         << " from cache, decompression ratio = "
                         << double(block->range_end()) /
                                double(block->uncompressed_size());
            ++blocks_evicted_;
            update_block_stats(*block);
          });
    }
  }

  std::future<block_range>
  get(size_t block_no, size_t offset, size_t size) const override {
    ++range_requests_;

    std::promise<block_range> promise;
    auto future = promise.get_future();

    // That is a mighty long lock, let's see how it works...
    std::lock_guard<std::mutex> lock(mx_);

    const auto range_end = offset + size;

    // See if the block is currently active (about-to-be decompressed)
    auto ia = active_.find(block_no);

    std::shared_ptr<block_request_set> brs;

    if (ia != active_.end()) {
      log_.debug() << "active sets found for block " << block_no;

      bool add_to_set = false;

      // Try to find a suitable request set to hook on to
      auto end =
          std::remove_if(ia->second.begin(), ia->second.end(),
                         [&brs, range_end, &add_to_set](
                             const std::weak_ptr<block_request_set>& wp) {
                           if (auto rs = wp.lock()) {
                             bool can_add_to_set = range_end <= rs->range_end();

                             if (!brs || (can_add_to_set && !add_to_set)) {
                               brs = std::move(rs);
                               add_to_set = can_add_to_set;
                             }
                             return false;
                           }
                           return true;
                         });

      // Remove all expired weak pointers
      ia->second.erase(end, ia->second.end());

      if (ia->second.empty()) {
        // No request sets left at all? M'kay.
        assert(!brs);
        active_.erase(ia);
      } else if (brs) {
        // That's the one
        // Check if by any chance the block has already
        // been decompressed far enough to fulfill the
        // promise immediately, otherwise add a new
        // request to the request set.

        log_.debug() << "block " << block_no << " found in active set";

        auto block = brs->block();

        if (range_end <= block->range_end()) {
          // We can immediately satisfy the promise
          promise.set_value(block_range(std::move(block), offset, size));
          ++active_hits_fast_;
        } else {
          if (!add_to_set) {
            // Make a new set for the same block
            brs =
                std::make_shared<block_request_set>(std::move(block), block_no);
          }

          // Promise will be fulfilled asynchronously
          brs->add(offset, range_end, std::move(promise));
          ++active_hits_slow_;

          if (!add_to_set) {
            ia->second.emplace_back(brs);
            enqueue_job(std::move(brs));
          }
        }

        return future;
      }

      log_.debug() << "block " << block_no << " not found in active set";
    }

    // See if it's cached (fully or partially decompressed)
    auto ic = cache_.find(block_no);

    if (ic != cache_.end()) {
      // Nice, at least the block is already there.

      log_.debug() << "block " << block_no << " found in cache";

      auto block = ic->second;

      if (range_end <= block->range_end()) {
        // We can immediately satisfy the promise
        promise.set_value(block_range(std::move(block), offset, size));
        ++cache_hits_fast_;
      } else {
        // Make a new set for the block
        brs = std::make_shared<block_request_set>(std::move(block), block_no);

        // Promise will be fulfilled asynchronously
        brs->add(offset, range_end, std::move(promise));
        ++cache_hits_slow_;

        active_[block_no].emplace_back(brs);
        enqueue_job(std::move(brs));
      }

      return future;
    }

    // Bummer. We don't know anything about the block.

    log_.debug() << "block " << block_no << " not found";

    assert(block_no < block_.size());

    auto block = std::make_shared<cached_block>(
        log_.get_logger(), block_[block_no], mm_, options_.mm_release);
    ++blocks_created_;

    // Make a new set for the block
    brs = std::make_shared<block_request_set>(std::move(block), block_no);

    // Promise will be fulfilled asynchronously
    brs->add(offset, range_end, std::move(promise));

    active_[block_no].emplace_back(brs);
    enqueue_job(std::move(brs));

    return future;
  }

 private:
  void update_block_stats(cached_block const& cb) {
    if (cb.range_end() < cb.uncompressed_size()) {
      ++partially_decompressed_;
    }
    total_decompressed_bytes_ += cb.range_end();
    total_block_bytes_ += cb.uncompressed_size();
  }

  void enqueue_job(std::shared_ptr<block_request_set> brs) const {
    // Lambda needs to be mutable so we can actually move out of it
    wg_.add_job([this, brs = std::move(brs)]() mutable {
      process_job(std::move(brs));
    });
  }

  void process_job(std::shared_ptr<block_request_set> brs) const {
    auto block_no = brs->block_no();

    log_.debug() << "processing block " << block_no;

    // Check if another worker is already processing this block
    {
      std::lock_guard<std::mutex> lock(mx_dec_);

      auto di = decompressing_.find(block_no);

      if (di != decompressing_.end()) {
        std::lock_guard<std::mutex> lock(mx_);

        if (auto other = di->second.lock()) {
          log_.debug() << "merging sets for block " << block_no;
          other->merge(std::move(*brs));
          ++sets_merged_;
          brs.reset();
          return;
        }
      }

      decompressing_[block_no] = brs;
    }

    auto block = brs->block();

    for (;;) {
      block_request req;
      bool is_last_req = false;

      // Fetch the next request, if any
      {
        std::lock_guard<std::mutex> lock(mx_);

        if (brs->empty()) {
          // This is absolutely crucial! At this point, we can no longer
          // allow other code to add to this request set, so we need to
          // expire all weak pointer from within this critial section.
          brs.reset();
          break;
        }

        req = brs->get();
        is_last_req = brs->empty();
      }

      // Process this request!

      size_t range_end = req.end();

      if (is_last_req) {
        auto max_end = block->uncompressed_size();
        double ratio = double(range_end) / double(max_end);
        if (ratio > options_.decompress_ratio) {
          log_.debug() << "block " << block_no << " over ratio: " << ratio
                       << " > " << options_.decompress_ratio;
          range_end = max_end;
        }
      }

      log_.debug() << "decompressing block " << block_no << " until position "
                   << req.end();

      try {
        block->decompress_until(range_end);
        req.fulfill(block);
      } catch (...) {
        req.error(std::current_exception());
      }
    }

    // Finally, put the block into the cache; it might already be
    // in there, in which case we just promote it to the front of
    // the LRU queue.
    {
      std::lock_guard<std::mutex> lock(mx_);
      cache_.set(block_no, std::move(block));
    }
  }

  using lru_type =
      folly::EvictingCacheMap<size_t, std::shared_ptr<cached_block>>;

  mutable std::mutex mx_;
  mutable lru_type cache_;
  mutable std::unordered_map<size_t,
                             std::deque<std::weak_ptr<block_request_set>>>
      active_;

  mutable std::mutex mx_dec_;
  mutable std::unordered_map<size_t, std::weak_ptr<block_request_set>>
      decompressing_;

  mutable std::atomic<size_t> blocks_created_{0};
  mutable std::atomic<size_t> blocks_evicted_{0};
  mutable std::atomic<size_t> sets_merged_{0};
  mutable std::atomic<size_t> range_requests_{0};
  mutable std::atomic<size_t> active_hits_fast_{0};
  mutable std::atomic<size_t> active_hits_slow_{0};
  mutable std::atomic<size_t> cache_hits_fast_{0};
  mutable std::atomic<size_t> cache_hits_slow_{0};
  mutable std::atomic<size_t> partially_decompressed_{0};
  mutable std::atomic<size_t> total_block_bytes_{0};
  mutable std::atomic<size_t> total_decompressed_bytes_{0};

  mutable worker_group wg_;
  std::vector<block> block_;
  std::shared_ptr<mmif> mm_;
  log_proxy<LoggerPolicy> log_;
  const block_cache_options options_;
};

block_cache::block_cache(logger& lgr, std::shared_ptr<mmif> mm,
                         const block_cache_options& options)
    : impl_(make_unique_logging_object<impl, block_cache_, logger_policies>(
          lgr, std::move(mm), options)) {}

// TODO: clean up: this is defined in fstypes.h...
block_range::block_range(std::shared_ptr<cached_block const> block,
                         size_t offset, size_t size)
    : begin_(block->data() + offset)
    , end_(begin_ + size)
    , block_(std::move(block)) {}

} // namespace dwarfs
