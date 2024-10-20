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
#include <chrono>
#include <condition_variable>
#include <exception>
#include <future>
#include <iterator>
#include <mutex>
#include <new>
#include <shared_mutex>
#include <thread>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include <folly/container/EvictingCacheMap.h>
#include <folly/container/F14Map.h>
#include <folly/stats/Histogram.h>
#include <folly/system/ThreadName.h>

#include <dwarfs/logger.h>
#include <dwarfs/mmif.h>
#include <dwarfs/performance_monitor.h>
#include <dwarfs/reader/block_cache_options.h>
#include <dwarfs/reader/cache_tidy_config.h>
#include <dwarfs/scope_exit.h>
#include <dwarfs/util.h>

#include <dwarfs/internal/fs_section.h>
#include <dwarfs/internal/worker_group.h>
#include <dwarfs/reader/internal/block_cache.h>
#include <dwarfs/reader/internal/cached_block.h>

namespace dwarfs::reader::internal {

using namespace dwarfs::internal;

namespace {

class sequential_access_detector {
 public:
  virtual ~sequential_access_detector() = default;

  virtual void set_block_count(size_t) = 0;
  virtual void touch(size_t block_no) = 0;
  virtual std::optional<size_t> prefetch() const = 0;
};

class no_sequential_access_detector : public sequential_access_detector {
 public:
  void set_block_count(size_t) override {}
  void touch(size_t) override {}
  std::optional<size_t> prefetch() const override { return std::nullopt; }
};

class lru_sequential_access_detector : public sequential_access_detector {
 public:
  explicit lru_sequential_access_detector(size_t seq_blocks)
      : lru_{seq_blocks}
      , seq_blocks_{seq_blocks} {}

  void set_block_count(size_t num_blocks) override {
    std::lock_guard lock(mx_);
    num_blocks_ = num_blocks;
    lru_.clear();
    is_sequential_.reset();
  }

  void touch(size_t block_no) override {
    std::lock_guard lock(mx_);
    lru_.set(block_no, block_no, true,
             [this](size_t, size_t&&) { is_sequential_.reset(); });
  }

  std::optional<size_t> prefetch() const override {
    std::lock_guard lock(mx_);

    if (lru_.size() < seq_blocks_) {
      return std::nullopt;
    }

    if (is_sequential_.has_value()) {
      return std::nullopt;
    }

    auto minmax = std::minmax_element(
        lru_.begin(), lru_.end(),
        [](auto const& a, auto const& b) { return a.first < b.first; });

    auto min = minmax.first->first;
    auto max = minmax.second->first;

    is_sequential_ = max - min + 1 == seq_blocks_;

    if (*is_sequential_ && max + 1 < num_blocks_) {
      return max + 1;
    }

    return std::nullopt;
  }

 private:
  using lru_type = folly::EvictingCacheMap<size_t, size_t>;

  std::mutex mutable mx_;
  lru_type lru_;
  std::optional<bool> mutable is_sequential_;
  size_t num_blocks_{0};
  size_t const seq_blocks_;
};

class block_request {
 public:
  block_request() = default;

  block_request(size_t begin, size_t end, std::promise<block_range>&& promise)
      : begin_(begin)
      , end_(end)
      , promise_(std::move(promise)) {
    DWARFS_CHECK(begin_ < end_, "invalid block_request");
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
      : block_(std::move(block))
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

  void merge(block_request_set& other) {
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

  std::shared_ptr<cached_block> block() const { return block_; }

  size_t block_no() const { return block_no_; }

 private:
  std::vector<block_request> queue_;
  size_t range_end_{0};
  std::shared_ptr<cached_block> block_;
  const size_t block_no_;
};

// multi-threaded block cache
template <typename LoggerPolicy>
class block_cache_ final : public block_cache::impl {
 public:
  block_cache_(logger& lgr, os_access const& os, std::shared_ptr<mmif> mm,
               block_cache_options const& options,
               std::shared_ptr<performance_monitor const> perfmon
               [[maybe_unused]])
      : cache_(0)
      , mm_(std::move(mm))
      , LOG_PROXY_INIT(lgr)
      // clang-format off
      PERFMON_CLS_PROXY_INIT(perfmon, "block_cache")
      PERFMON_CLS_TIMER_INIT(get, "block_no", "offset", "size")
      PERFMON_CLS_TIMER_INIT(process, "block_no")
      PERFMON_CLS_TIMER_INIT(decompress, "range_end") // clang-format on
      , seq_access_detector_{create_seq_access_detector(
            options.sequential_access_detector_threshold)}
      , os_{os}
      , options_(options) {
    if (options.init_workers) {
      wg_ = worker_group(lgr, os_, "blkcache",
                         std::max(options.num_workers > 0
                                      ? options.num_workers
                                      : hardware_concurrency(),
                                  static_cast<size_t>(1)));
    }
  }

  ~block_cache_() noexcept override {
    LOG_DEBUG << "stopping cache workers";

    if (tidy_running_) {
      stop_tidy_thread();
    }

    if (wg_) {
      wg_.stop();
    }

    if (!blocks_created_.load()) {
      return;
    }

    LOG_DEBUG << "cached blocks:";

    for (const auto& cb : cache_) {
      LOG_DEBUG << "  block " << cb.first << ", decompression ratio = "
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

    // The same block can be evicted multiple times. Active requests may hold
    // on to a block that has been evicted from the cache and re-insert the
    // block after the request is complete. So it's not a bug to see the
    // number of evicted blocks outgrow the number of created blocks.
    LOG_VERBOSE << "blocks created: " << blocks_created_.load();
    LOG_VERBOSE << "blocks evicted: " << blocks_evicted_.load();
    LOG_VERBOSE << "blocks tidied: " << blocks_tidied_.load();
    LOG_VERBOSE << "request sets merged: " << sets_merged_.load();
    LOG_VERBOSE << "total requests: " << range_requests_.load();
    LOG_VERBOSE << "sequential prefetches: " << sequential_prefetches_.load();
    LOG_VERBOSE << "active hits (fast): " << active_hits_fast_.load();
    LOG_VERBOSE << "active hits (slow): " << active_hits_slow_.load();
    LOG_VERBOSE << "cache hits (fast): " << cache_hits_fast_.load();
    LOG_VERBOSE << "cache hits (slow): " << cache_hits_slow_.load();

    LOG_VERBOSE << "total bytes decompressed: " << total_decompressed_bytes_;
    LOG_VERBOSE << "average block decompression: "
                << fmt::format("{:.1f}", avg_decompression) << "%";

    LOG_VERBOSE << "fast hit rate: " << fmt::format("{:.3f}", fast_hit_rate)
                << "%";
    LOG_VERBOSE << "slow hit rate: " << fmt::format("{:.3f}", slow_hit_rate)
                << "%";
    LOG_VERBOSE << "miss rate: " << fmt::format("{:.3f}", miss_rate) << "%";

    LOG_VERBOSE << "expired active requests: " << active_expired_.load();

    auto active_pct = [&](double p) {
      return active_set_size_.getPercentileEstimate(p);
    };

    LOG_VERBOSE << "active set size p50: " << active_pct(0.5)
                << ", p75: " << active_pct(0.75) << ", p90: " << active_pct(0.9)
                << ", p95: " << active_pct(0.95)
                << ", p99: " << active_pct(0.99);
  }

  size_t block_count() const override { return block_.size(); }

  void insert(fs_section const& section) override {
    block_.emplace_back(section);
    seq_access_detector_->set_block_count(block_.size());
  }

  void set_block_size(size_t size) override {
    // XXX: This currently inevitably clears the cache
    if (size == 0) {
      DWARFS_THROW(runtime_error, "block size is zero");
    }
    auto max_blocks = std::max<size_t>(options_.max_bytes / size, 1);

    if (!block_.empty() && max_blocks > block_.size()) {
      max_blocks = block_.size();
    }

    std::lock_guard lock(mx_);
    cache_.~lru_type();
    new (&cache_) lru_type(max_blocks);
    cache_.setPruneHook(
        [this](size_t block_no, std::shared_ptr<cached_block>&& block) {
          LOG_DEBUG << "evicting block " << block_no
                    << " from cache, decompression ratio = "
                    << double(block->range_end()) /
                           double(block->uncompressed_size());
          blocks_evicted_.fetch_add(1, std::memory_order_relaxed);
          update_block_stats(*block);
        });
  }

  void set_num_workers(size_t num) override {
    std::unique_lock lock(mx_wg_);

    if (wg_) {
      wg_.stop();
    }

    wg_ = worker_group(LOG_GET_LOGGER, os_, "blkcache", num);
  }

  void set_tidy_config(cache_tidy_config const& cfg) override {
    if (cfg.strategy == cache_tidy_strategy::NONE) {
      if (tidy_running_) {
        stop_tidy_thread();
      }
    } else {
      if (cfg.interval == std::chrono::milliseconds::zero()) {
        DWARFS_THROW(runtime_error, "tidy interval is zero");
      }

      std::lock_guard lock(mx_);

      tidy_config_ = cfg;

      if (tidy_running_) {
        tidy_cond_.notify_all();
      } else {
        tidy_running_ = true;
        tidy_thread_ = std::thread(&block_cache_::tidy_thread, this);
      }
    }
  }

  std::future<block_range>
  get(size_t block_no, size_t offset, size_t size) const override {
    PERFMON_CLS_SCOPED_SECTION(get)
    PERFMON_SET_CONTEXT(block_no, offset, size)

    seq_access_detector_->touch(block_no);

    scope_exit do_prefetch{[&] {
      if (auto next = seq_access_detector_->prefetch()) {
        sequential_prefetches_.fetch_add(1, std::memory_order_relaxed);

        {
          std::lock_guard lock(mx_);
          create_cached_block(*next, std::promise<block_range>{}, 0,
                              std::numeric_limits<size_t>::max());
        }
      }
    }};

    range_requests_.fetch_add(1, std::memory_order_relaxed);

    std::promise<block_range> promise;
    auto future = promise.get_future();

    // First, let's see if it's an uncompressed block, in which case we
    // can completely bypass the cache
    try {
      if (block_no >= block_.size()) {
        DWARFS_THROW(runtime_error,
                     fmt::format("block number out of range {0} >= {1}",
                                 block_no, block_.size()));
      }

      auto const& section = DWARFS_NOTHROW(block_.at(block_no));

      if (section.compression() == compression_type::NONE) {
        LOG_TRACE << "block " << block_no
                  << " is uncompressed, bypassing cache";
        promise.set_value(block_range(section.data(*mm_).data(), offset, size));
        return future;
      }
    } catch (...) {
      promise.set_exception(std::current_exception());
      return future;
    }

    // That is a mighty long lock, let's see how it works...
    std::lock_guard lock(mx_);

    const auto range_end = offset + size;

    // See if the block is currently active (about-to-be decompressed)
    auto ia = active_.find(block_no);

    std::shared_ptr<block_request_set> brs;

    if (ia != active_.end()) {
      LOG_TRACE << "active sets found for block " << block_no;

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

      if (end != ia->second.end()) {
        active_expired_.fetch_add(std::distance(end, ia->second.end()),
                                  std::memory_order_relaxed);

        // Remove all expired weak pointers
        ia->second.erase(end, ia->second.end());
      }

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

        LOG_TRACE << "block " << block_no << " found in active set";

        auto block = brs->block();

        if (range_end <= block->range_end()) {
          // We can immediately satisfy the promise
          promise.set_value(block_range(std::move(block), offset, size));
          active_hits_fast_.fetch_add(1, std::memory_order_relaxed);
        } else {
          if (!add_to_set) {
            // Make a new set for the same block
            brs =
                std::make_shared<block_request_set>(std::move(block), block_no);
          }

          // Promise will be fulfilled asynchronously
          brs->add(offset, range_end, std::move(promise));
          active_hits_slow_.fetch_add(1, std::memory_order_relaxed);

          if (!add_to_set) {
            ia->second.emplace_back(brs);
            active_set_size_.addValue(ia->second.size());
            enqueue_job(std::move(brs));
          }
        }

        return future;
      }

      LOG_TRACE << "block " << block_no << " not found in active set";
    }

    // See if it's cached (fully or partially decompressed)
    auto ic = cache_.find(block_no);

    if (ic != cache_.end()) {
      // Nice, at least the block is already there.

      LOG_TRACE << "block " << block_no << " found in cache";

      auto block = ic->second;

      if (range_end <= block->range_end()) {
        // We can immediately satisfy the promise
        promise.set_value(block_range(std::move(block), offset, size));
        cache_hits_fast_.fetch_add(1, std::memory_order_relaxed);
      } else {
        // Make a new set for the block
        brs = std::make_shared<block_request_set>(std::move(block), block_no);

        // Promise will be fulfilled asynchronously
        brs->add(offset, range_end, std::move(promise));
        cache_hits_slow_.fetch_add(1, std::memory_order_relaxed);

        auto& active = active_[block_no];
        active.emplace_back(brs);
        active_set_size_.addValue(active.size());
        enqueue_job(std::move(brs));
      }

      return future;
    }

    // Bummer. We don't know anything about the block.

    LOG_TRACE << "block " << block_no << " not found";

    create_cached_block(block_no, std::move(promise), offset, range_end);

    return future;
  }

 private:
  static std::unique_ptr<sequential_access_detector>
  create_seq_access_detector(size_t threshold) {
    if (threshold == 0) {
      return std::make_unique<no_sequential_access_detector>();
    }

    return std::make_unique<lru_sequential_access_detector>(threshold);
  }

  void create_cached_block(size_t block_no, std::promise<block_range>&& promise,
                           size_t offset, size_t range_end) const {
    try {
      std::shared_ptr<cached_block> block = cached_block::create(
          LOG_GET_LOGGER, DWARFS_NOTHROW(block_.at(block_no)), mm_,
          options_.mm_release, options_.disable_block_integrity_check);
      blocks_created_.fetch_add(1, std::memory_order_relaxed);

      // Make a new set for the block
      auto brs =
          std::make_shared<block_request_set>(std::move(block), block_no);

      // Promise will be fulfilled asynchronously
      brs->add(offset, range_end, std::move(promise));

      auto& active = active_[block_no];
      active.emplace_back(brs);
      active_set_size_.addValue(active.size());
      enqueue_job(std::move(brs));
    } catch (...) {
      promise.set_exception(std::current_exception());
    }
  }

  void stop_tidy_thread() {
    {
      std::lock_guard lock(mx_);
      tidy_running_ = false;
    }
    tidy_cond_.notify_all();
    tidy_thread_.join();
  }

  void update_block_stats(cached_block const& cb) {
    if (cb.range_end() < cb.uncompressed_size()) {
      partially_decompressed_.fetch_add(1, std::memory_order_relaxed);
    }
    total_decompressed_bytes_.fetch_add(cb.range_end(),
                                        std::memory_order_relaxed);
    total_block_bytes_.fetch_add(cb.uncompressed_size(),
                                 std::memory_order_relaxed);
  }

  void enqueue_job(std::shared_ptr<block_request_set> brs) const {
    std::shared_lock lock(mx_wg_);

    // Lambda needs to be mutable so we can actually move out of it
    wg_.add_job([this, brs = std::move(brs)]() mutable {
      process_job(std::move(brs));
    });
  }

  void process_job(std::shared_ptr<block_request_set> brs) const {
    PERFMON_CLS_SCOPED_SECTION(process)

    auto block_no = brs->block_no();
    PERFMON_SET_CONTEXT(block_no)

    LOG_TRACE << "processing block " << block_no;

    // Check if another worker is already processing this block
    {
      std::lock_guard lock_dec(mx_dec_);

      auto di = decompressing_.find(block_no);

      if (di != decompressing_.end()) {
        std::lock_guard lock(mx_);

        if (auto other = di->second.lock()) {
          LOG_TRACE << "merging sets for block " << block_no;
          other->merge(*brs);
          sets_merged_.fetch_add(1, std::memory_order_relaxed);
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
        std::lock_guard lock(mx_);

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
      auto max_end = block->uncompressed_size();

      if (range_end == std::numeric_limits<size_t>::max()) {
        range_end = max_end;
      }

      if (is_last_req) {
        double ratio = double(range_end) / double(max_end);
        if (ratio > options_.decompress_ratio) {
          LOG_TRACE << "block " << block_no << " over ratio: " << ratio << " > "
                    << options_.decompress_ratio;
          range_end = max_end;
        }
      }

      try {
        if (range_end > block->range_end()) {
          PERFMON_CLS_SCOPED_SECTION(decompress)
          PERFMON_SET_CONTEXT(range_end)

          LOG_TRACE << "decompressing block " << block_no << " until position "
                    << range_end;

          block->decompress_until(range_end);
        }

        req.fulfill(block);
      } catch (...) {
        req.error(std::current_exception());
      }
    }

    // Finally, put the block into the cache; it might already be
    // in there, in which case we just promote it to the front of
    // the LRU queue.
    {
      std::lock_guard lock(mx_);

      if (tidy_config_.strategy == cache_tidy_strategy::EXPIRY_TIME) {
        block->touch();
      }

      cache_.set(block_no, std::move(block));
    }
  }

  template <typename Pred>
  void remove_block_if(Pred const& predicate) {
    auto it = cache_.begin();

    while (it != cache_.end()) {
      if (predicate(*it->second)) {
        it = cache_.erase(it);
        blocks_tidied_.fetch_add(1, std::memory_order_relaxed);
      } else {
        ++it;
      }
    }
  }

  void tidy_thread() {
    folly::setThreadName("cache-tidy");

    std::unique_lock lock(mx_);

    while (tidy_running_) {
      if (tidy_cond_.wait_for(lock, tidy_config_.interval) ==
          std::cv_status::timeout) {
        switch (tidy_config_.strategy) {
        case cache_tidy_strategy::EXPIRY_TIME:
          remove_block_if(
              [tp = std::chrono::steady_clock::now() -
                    tidy_config_.expiry_time](cached_block const& blk) {
                return blk.last_used_before(tp);
              });
          break;

        case cache_tidy_strategy::BLOCK_SWAPPED_OUT: {
          std::vector<uint8_t> tmp;
          remove_block_if([&tmp](cached_block const& blk) {
            return blk.any_pages_swapped_out(tmp);
          });
        } break;

        default:
          break;
        }
      }
    }
  }

  using lru_type =
      folly::EvictingCacheMap<size_t, std::shared_ptr<cached_block>>;

  mutable std::mutex mx_;
  mutable lru_type cache_;
  mutable folly::F14FastMap<size_t,
                            std::vector<std::weak_ptr<block_request_set>>>
      active_;
  std::thread tidy_thread_;
  std::condition_variable tidy_cond_;
  bool tidy_running_{false};

  mutable std::mutex mx_dec_;
  mutable folly::F14FastMap<size_t, std::weak_ptr<block_request_set>>
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
  mutable std::atomic<size_t> blocks_tidied_{0};
  mutable std::atomic<size_t> active_expired_{0};
  mutable std::atomic<size_t> sequential_prefetches_{0};
  mutable folly::Histogram<size_t> active_set_size_{1, 0, 1024};

  mutable std::shared_mutex mx_wg_;
  mutable worker_group wg_;
  std::vector<fs_section> block_;
  std::shared_ptr<mmif> mm_;
  LOG_PROXY_DECL(LoggerPolicy);
  PERFMON_CLS_PROXY_DECL
  PERFMON_CLS_TIMER_DECL(get)
  PERFMON_CLS_TIMER_DECL(process)
  PERFMON_CLS_TIMER_DECL(decompress)
  std::unique_ptr<sequential_access_detector> seq_access_detector_;
  os_access const& os_;
  const block_cache_options options_;
  cache_tidy_config tidy_config_;
};

} // namespace

block_cache::block_cache(logger& lgr, os_access const& os,
                         std::shared_ptr<mmif> mm,
                         const block_cache_options& options,
                         std::shared_ptr<performance_monitor const> perfmon)
    : impl_(make_unique_logging_object<impl, block_cache_, logger_policies>(
          lgr, os, std::move(mm), options, std::move(perfmon))) {}

} // namespace dwarfs::reader::internal
