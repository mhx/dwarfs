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
#include <cstdint>
#include <cstring>
#include <deque>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <boost/align.hpp>

#include <fmt/format.h>

#include <parallel_hashmap/phmap.h>

#include <folly/hash/Hash.h>
#include <folly/small_vector.h>
#include <folly/stats/Histogram.h>

#include "dwarfs/block_data.h"
#include "dwarfs/block_manager.h"
#include "dwarfs/chunkable.h"
#include "dwarfs/compression_constraints.h"
#include "dwarfs/cyclic_hash.h"
#include "dwarfs/entry.h"
#include "dwarfs/error.h"
#include "dwarfs/logger.h"
#include "dwarfs/progress.h"
#include "dwarfs/segmenter.h"
#include "dwarfs/util.h"

namespace dwarfs {

namespace {

/**
 * Segmenter Strategy
 *
 * For each *block*, start new rolling hash. The hashes are associcated
 * with the block, new hash-offset-pairs will only be added as the block
 * grows. We only need to store a hash-offset-pair every N bytes, with N
 * being configurable (typically half of the window size so we find all
 * matches of at least 1.5 times the window size, but could also be smaller).
 *
 * For each *file*, start new rolling hash. Hash values *expire* immediately,
 * no history of hash values is kept. Up to n blocks (typically only one)
 * will be scanned for matches. Old file data beyond the moving window will
 * be added to the current *block*, causing the rolling *block* hash to also
 * advance. Data needs to be added to the block only in increments at which
 * a new hash valus is computed.
 *
 * This strategy ensures that we're using a deterministic amount of memory
 * (proportional to block size and history block count).
 *
 * A single window size is sufficient. That window size should still be
 * configurable.
 */

struct segmenter_stats {
  segmenter_stats()
      : l2_collision_vec_size(1, 0, 128) {}

  size_t total_hashes{0};
  size_t l2_collisions{0};
  size_t total_matches{0};
  size_t good_matches{0};
  size_t bad_matches{0};
  size_t bloom_lookups{0};
  size_t bloom_hits{0};
  size_t bloom_true_positives{0};
  folly::Histogram<size_t> l2_collision_vec_size;
};

template <typename KeyT, typename ValT, size_t MaxCollInline = 2>
class fast_multimap {
 private:
  using collision_vector = folly::small_vector<ValT, MaxCollInline>;
  using blockhash_t = phmap::flat_hash_map<KeyT, ValT>;
  using collision_t = phmap::flat_hash_map<KeyT, collision_vector>;

 public:
  void insert(KeyT const& key, ValT const& val) {
    if (!values_.insert(std::make_pair(key, val)).second) [[unlikely]] {
      collisions_[key].emplace_back(val);
    }
  }

  template <typename F>
  void for_each_value(KeyT const& key, F&& func) const {
    if (auto it = values_.find(key); it != values_.end()) [[unlikely]] {
      func(it->second);
      if (auto it2 = collisions_.find(key); it2 != collisions_.end())
          [[unlikely]] {
        for (auto const& val : it2->second) {
          func(val);
        }
      }
    }
  }

  void clear() {
    values_.clear();
    collisions_.clear();
  }

  blockhash_t const& values() const { return values_; };
  collision_t const& collisions() const { return collisions_; };

 private:
  blockhash_t values_;
  collision_t collisions_;
};

constexpr unsigned bitcount(unsigned n) {
  return n > 0 ? (n & 1) + bitcount(n >> 1) : 0;
}

constexpr uint64_t pow2ceil(uint64_t n) {
  n--;
  n |= n >> 1;
  n |= n >> 2;
  n |= n >> 4;
  n |= n >> 8;
  n |= n >> 16;
  n |= n >> 32;
  n++;
  return n;
}

/**
 * A very simple bloom filter. This is not generalized at all and highly
 * optimized for the cyclic hash use case.
 *
 * - Since we're already using a hash value, there's no need to hash the
 *   value before accessing the bloom filter bit field.
 *
 * - We can accept a high false positive rate as the secondary lookup
 *   is not very expensive. However, the bloom filter lookup must be
 *   extremely cheap, so we can't afford e.g. using two hashes instead
 *   of one.
 */
class alignas(64) bloom_filter {
 public:
  using bits_type = uint64_t;

  static constexpr size_t value_mask = 8 * sizeof(bits_type) - 1;
  static constexpr size_t index_shift = bitcount(value_mask);
  static constexpr size_t alignment = 64;

  explicit bloom_filter(size_t size)
      : index_mask_{(std::max(size, value_mask + 1) >> index_shift) - 1}
      , size_{std::max(size, value_mask + 1)} {
    if (size & (size - 1)) {
      throw std::runtime_error("size must be a power of two");
    }
    bits_ = reinterpret_cast<bits_type*>(
        boost::alignment::aligned_alloc(alignment, size_ / 8));
    if (!bits_) {
      throw std::runtime_error("failed to allocate aligned memory");
    }
    clear();
  }

  ~bloom_filter() { boost::alignment::aligned_free(bits_); }

  void add(size_t ix) {
    auto bits = bits_;
    BOOST_ALIGN_ASSUME_ALIGNED(bits, sizeof(bits_type));
    bits[(ix >> index_shift) & index_mask_] |= static_cast<bits_type>(1)
                                               << (ix & value_mask);
  }

  bool test(size_t ix) const {
    auto bits = bits_;
    BOOST_ALIGN_ASSUME_ALIGNED(bits, sizeof(bits_type));
    return bits[(ix >> index_shift) & index_mask_] &
           (static_cast<bits_type>(1) << (ix & value_mask));
  }

  // size in bits
  size_t size() const { return size_; }

  void clear() { std::fill(begin(), end(), 0); }

  void merge(bloom_filter const& other) {
    if (size() != other.size()) {
      throw std::runtime_error("size mismatch");
    }
    std::transform(cbegin(), cend(), other.cbegin(), begin(), std::bit_or<>{});
  }

 private:
  bits_type const* cbegin() const { return bits_; }
  bits_type const* cend() const { return bits_ + (size_ >> index_shift); }
  bits_type const* begin() const { return bits_; }
  bits_type const* end() const { return bits_ + (size_ >> index_shift); }
  bits_type* begin() { return bits_; }
  bits_type* end() { return bits_ + (size_ >> index_shift); }

  bits_type* bits_;
  size_t const index_mask_;
  size_t const size_;
};

template <size_t N>
class ConstantGranularityPolicy {
 public:
  template <typename T>
  void add_new_block(T& blocks, size_t num, size_t size, size_t window_size,
                     size_t window_step, size_t bloom_filter_size) {
    blocks.emplace_back(num, size, window_size, window_step, bloom_filter_size);
  }
};

class VariableGranularityPolicy {
 public:
  VariableGranularityPolicy(uint32_t granularity)
      : granularity_{granularity} {}

  template <typename T>
  void add_new_block(T& blocks, size_t num, size_t size, size_t window_size,
                     size_t window_step, size_t bloom_filter_size) {
    blocks.emplace_back(num, size, window_size, window_step, bloom_filter_size,
                        granularity_);
  }

 private:
  uint_fast32_t granularity_;
};

template <typename GranularityPolicy>
class active_block : private GranularityPolicy {
 private:
  using offset_t = uint32_t;
  using hash_t = uint32_t;

 public:
  template <typename... PolicyArgs>
  active_block(size_t num, size_t size, size_t window_size, size_t window_step,
               size_t bloom_filter_size, PolicyArgs&&... args)
      : GranularityPolicy(std::forward<PolicyArgs>(args)...)
      , num_(num)
      , capacity_(size)
      , window_size_(window_size)
      , window_step_mask_(window_step - 1)
      , filter_(bloom_filter_size)
      , data_{std::make_shared<block_data>()} {
    DWARFS_CHECK((window_step & window_step_mask_) == 0,
                 "window step size not a power of two");
    data_->reserve(capacity_);
  }

  size_t num() const { return num_; }
  size_t size() const { return data_->size(); }

  bool full() const { return size() == capacity_; }
  std::shared_ptr<block_data> data() const { return data_; }

  void append(std::span<uint8_t const> data, bloom_filter* filter);

  size_t next_hash_distance() const {
    return window_step_mask_ + 1 - (data_->size() & window_step_mask_);
  }

  template <typename F>
  void for_each_offset(hash_t key, F&& func) const {
    offsets_.for_each_value(key, std::forward<F>(func));
  }

  template <typename F>
  void for_each_offset_filter(hash_t key, F&& func) const {
    if (filter_.test(key)) [[unlikely]] {
      offsets_.for_each_value(key, std::forward<F>(func));
    }
  }

  void finalize(segmenter_stats& stats) {
    stats.total_hashes += offsets_.values().size();
    for (auto& c : offsets_.collisions()) {
      stats.total_hashes += c.second.size();
      stats.l2_collisions += c.second.size() - 1;
      stats.l2_collision_vec_size.addValue(c.second.size());
    }
  }

  bloom_filter const& filter() const { return filter_; }

 private:
  static constexpr size_t num_inline_offsets = 4;

  size_t const num_, capacity_, window_size_, window_step_mask_;
  rsync_hash hasher_;
  bloom_filter filter_;
  fast_multimap<hash_t, offset_t, num_inline_offsets> offsets_;
  std::shared_ptr<block_data> data_;
};

template <typename LoggerPolicy, typename GranularityPolicy>
class segmenter_ final : public segmenter::impl, private GranularityPolicy {
 public:
  template <typename... PolicyArgs>
  segmenter_(logger& lgr, progress& prog, std::shared_ptr<block_manager> blkmgr,
             segmenter::config const& cfg,
             segmenter::block_ready_cb block_ready, PolicyArgs&&... args)
      : GranularityPolicy(std::forward<PolicyArgs>(args)...)
      , LOG_PROXY_INIT(lgr)
      , prog_{prog}
      , blkmgr_{std::move(blkmgr)}
      , cfg_{cfg}
      , block_ready_{std::move(block_ready)}
      , window_size_{window_size(cfg)}
      , window_step_{window_step(cfg)}
      , block_size_{block_size(cfg)}
      , filter_{bloom_filter_size(cfg)}
      , match_counts_{1, 0, 128} {
    if (segmentation_enabled()) {
      LOG_INFO << "using a " << size_with_unit(window_size_) << " window at "
               << size_with_unit(window_step_) << " steps for segment analysis";
      LOG_INFO << "bloom filter size: " << size_with_unit(filter_.size() / 8);
    }
  }

  ~segmenter_() {
    auto pct = [&](double p) { return match_counts_.getPercentileEstimate(p); };
    LOG_DEBUG << "match counts p50: " << pct(0.5) << ", p75: " << pct(0.75)
              << ", p90: " << pct(0.9) << ", p95: " << pct(0.95)
              << ", p99: " << pct(0.99);
  }

  void add_chunkable(chunkable& chkable) override;
  void finish() override;

 private:
  struct chunk_state {
    size_t offset{0};
    size_t size{0};
  };

  bool segmentation_enabled() const {
    return cfg_.max_active_blocks > 0 and window_size_ > 0;
  }

  void block_ready();
  void finish_chunk(chunkable& chkable);
  void append_to_block(chunkable& chkable, size_t offset, size_t size);
  void add_data(chunkable& chkable, size_t offset, size_t size);
  void segment_and_add_data(chunkable& chkable, size_t size);

  static size_t bloom_filter_size(const segmenter::config& cfg) {
    auto hash_count = pow2ceil(std::max<size_t>(1, cfg.max_active_blocks)) *
                      (block_size(cfg) / window_step(cfg));
    return (static_cast<size_t>(1) << cfg.bloom_filter_size) * hash_count;
  }

  static size_t window_size(const segmenter::config& cfg) {
    return cfg.blockhash_window_size > 0
               ? static_cast<size_t>(1) << cfg.blockhash_window_size
               : 0;
  }

  static size_t window_step(const segmenter::config& cfg) {
    return std::max<size_t>(1, window_size(cfg) >> cfg.window_increment_shift);
  }

  static size_t block_size(const segmenter::config& cfg) {
    return static_cast<size_t>(1) << cfg.block_size_bits;
  }

  LOG_PROXY_DECL(LoggerPolicy);
  progress& prog_;
  std::shared_ptr<block_manager> blkmgr_;
  segmenter::config const cfg_;
  segmenter::block_ready_cb block_ready_;

  size_t const window_size_;
  size_t const window_step_;
  size_t const block_size_;

  chunk_state chunk_;

  bloom_filter filter_;

  segmenter_stats stats_;

  using active_block_type = active_block<GranularityPolicy>;

  // Active blocks are blocks that can still be referenced from new chunks.
  // Up to N blocks (configurable) can be active and are kept in this queue.
  // All active blocks except for the last one are immutable and potentially
  // already being compressed.
  std::deque<active_block_type> blocks_;

  folly::Histogram<size_t> match_counts_;
};

template <typename GranularityPolicy>
class segment_match {
 public:
  using active_block_type = active_block<GranularityPolicy>;

  segment_match(active_block_type const* blk, uint32_t off) noexcept
      : block_{blk}
      , offset_{off} {}

  void verify_and_extend(uint8_t const* pos, size_t len, uint8_t const* begin,
                         uint8_t const* end);

  bool operator<(segment_match const& rhs) const {
    return size_ < rhs.size_ ||
           (size_ == rhs.size_ &&
            (block_->num() < rhs.block_->num() ||
             (block_->num() == rhs.block_->num() && offset_ < rhs.offset_)));
  }

  uint8_t const* data() const { return data_; }
  uint32_t size() const { return size_; }
  uint32_t offset() const { return offset_; }
  size_t block_num() const { return block_->num(); }

 private:
  active_block_type const* block_;
  uint32_t offset_;
  uint32_t size_{0};
  uint8_t const* data_{nullptr};
};

template <typename GranularityPolicy>
void active_block<GranularityPolicy>::append(std::span<uint8_t const> data,
                                             bloom_filter* filter) {
  auto& v = data_->vec();
  auto offset = v.size();
  DWARFS_CHECK(offset + data.size() <= capacity_,
               fmt::format("block capacity exceeded: {} + {} > {}", offset,
                           data.size(), capacity_));
  v.resize(offset + data.size());
  ::memcpy(v.data() + offset, data.data(), data.size());

  if (window_size_ > 0) {
    while (offset < v.size()) {
      if (offset < window_size_) [[unlikely]] {
        hasher_.update(v[offset++]);
      } else {
        hasher_.update(v[offset - window_size_], v[offset]);
        if ((++offset & window_step_mask_) == 0) [[unlikely]] {
          auto hashval = hasher_();
          offsets_.insert(hashval, offset - window_size_);
          filter_.add(hashval);
          if (filter) {
            filter->add(hashval);
          }
        }
      }
    }
  }
}

template <typename GranularityPolicy>
void segment_match<GranularityPolicy>::verify_and_extend(uint8_t const* pos,
                                                         size_t len,
                                                         uint8_t const* begin,
                                                         uint8_t const* end) {
  auto const& v = block_->data()->vec();

  if (::memcmp(v.data() + offset_, pos, len) == 0) {
    // scan backward
    auto tmp = offset_;
    while (tmp > 0 && pos > begin && v[tmp - 1] == pos[-1]) {
      --tmp;
      --pos;
    }
    len += offset_ - tmp;
    offset_ = tmp;
    data_ = pos;

    // scan forward
    pos += len;
    tmp = offset_ + len;
    while (tmp < v.size() && pos < end && v[tmp] == *pos) {
      ++tmp;
      ++pos;
    }
    size_ = tmp - offset_;
  }
}

template <typename LoggerPolicy, typename GranularityPolicy>
void segmenter_<LoggerPolicy, GranularityPolicy>::add_chunkable(
    chunkable& chkable) {
  if (auto size = chkable.size(); size > 0) {
    LOG_TRACE << "adding " << chkable.description();

    // if (granularity_ > 1) {
    //   DWARFS_CHECK(
    //       size % granularity_ == 0,
    //       fmt::format(
    //           "unexpected size {} for given granularity {} (modulus: {})",
    //           size, granularity_, size % granularity_));
    // }

    if (!segmentation_enabled() or size < window_size_) {
      // no point dealing with hashing, just write it out
      add_data(chkable, 0, size);
      finish_chunk(chkable);
    } else {
      segment_and_add_data(chkable, size);
    }
  }
}

template <typename LoggerPolicy, typename GranularityPolicy>
void segmenter_<LoggerPolicy, GranularityPolicy>::finish() {
  if (!blocks_.empty() && !blocks_.back().full()) {
    block_ready();
  }

  auto l1_collisions = stats_.l2_collision_vec_size.computeTotalCount();

  if (stats_.bloom_lookups > 0) {
    LOG_INFO << "bloom filter reject rate: "
             << fmt::format("{:.3f}%", 100.0 - 100.0 * stats_.bloom_hits /
                                                   stats_.bloom_lookups)
             << " (TPR="
             << fmt::format("{:.3f}%", 100.0 * stats_.bloom_true_positives /
                                           stats_.bloom_hits)
             << ", lookups=" << stats_.bloom_lookups << ")";
  }
  if (stats_.total_matches > 0) {
    LOG_INFO << "segmentation matches: good=" << stats_.good_matches
             << ", bad=" << stats_.bad_matches
             << ", total=" << stats_.total_matches;
  }
  if (stats_.total_hashes > 0) {
    LOG_INFO << "segmentation collisions: L1="
             << fmt::format("{:.3f}%",
                            100.0 * (l1_collisions + stats_.l2_collisions) /
                                stats_.total_hashes)
             << ", L2="
             << fmt::format("{:.3f}%",
                            100.0 * stats_.l2_collisions / stats_.total_hashes)
             << " [" << stats_.total_hashes << " hashes]";
  }

  if (l1_collisions > 0) {
    auto pct = [&](double p) {
      return stats_.l2_collision_vec_size.getPercentileEstimate(p);
    };
    LOG_DEBUG << "collision vector size p50: " << pct(0.5)
              << ", p75: " << pct(0.75) << ", p90: " << pct(0.9)
              << ", p95: " << pct(0.95) << ", p99: " << pct(0.99);
  }
}

template <typename LoggerPolicy, typename GranularityPolicy>
void segmenter_<LoggerPolicy, GranularityPolicy>::block_ready() {
  auto& block = blocks_.back();
  block.finalize(stats_);
  auto written_block_num = block_ready_(block.data());
  blkmgr_->set_written_block(block.num(), written_block_num);
  ++prog_.block_count;
}

template <typename LoggerPolicy, typename GranularityPolicy>
void segmenter_<LoggerPolicy, GranularityPolicy>::append_to_block(
    chunkable& chkable, size_t offset, size_t size) {
  if (blocks_.empty() or blocks_.back().full()) [[unlikely]] {
    if (blocks_.size() >= std::max<size_t>(1, cfg_.max_active_blocks)) {
      blocks_.pop_front();
    }

    filter_.clear();
    for (auto const& b : blocks_) {
      filter_.merge(b.filter());
    }

    this->add_new_block(blocks_, blkmgr_->get_logical_block(), block_size_,
                        cfg_.max_active_blocks > 0 ? window_size_ : 0,
                        window_step_, filter_.size());
  }

  auto& block = blocks_.back();

  block.append(chkable.span().subspan(offset, size), &filter_);
  chunk_.size += size;

  prog_.filesystem_size += size;

  if (block.full()) [[unlikely]] {
    chkable.release_until(offset + size);
    finish_chunk(chkable);
    block_ready();
  }
}

template <typename LoggerPolicy, typename GranularityPolicy>
void segmenter_<LoggerPolicy, GranularityPolicy>::add_data(chunkable& chkable,
                                                           size_t offset,
                                                           size_t size) {
  while (size > 0) {
    size_t block_offset = 0;

    if (!blocks_.empty()) {
      block_offset = blocks_.back().size();
    }

    size_t chunk_size = std::min(size, block_size_ - block_offset);

    append_to_block(chkable, offset, chunk_size);

    offset += chunk_size;
    size -= chunk_size;
  }
}

template <typename LoggerPolicy, typename GranularityPolicy>
void segmenter_<LoggerPolicy, GranularityPolicy>::finish_chunk(
    chunkable& chkable) {
  if (chunk_.size > 0) {
    auto& block = blocks_.back();
    chkable.add_chunk(block.num(), chunk_.offset, chunk_.size);
    chunk_.offset = block.full() ? 0 : block.size();
    chunk_.size = 0;
    prog_.chunk_count++;
  }
}

template <typename LoggerPolicy, typename GranularityPolicy>
void segmenter_<LoggerPolicy, GranularityPolicy>::segment_and_add_data(
    chunkable& chkable, size_t size) {
  rsync_hash hasher;
  size_t offset = 0;
  size_t written = 0;
  size_t lookback_size = window_size_ + window_step_;
  size_t next_hash_offset =
      lookback_size +
      (blocks_.empty() ? window_step_ : blocks_.back().next_hash_distance());
  auto data = chkable.span();
  auto p = data.data();

  DWARFS_CHECK(size >= window_size_, "unexpected call to segment_and_add_data");

  for (; offset < window_size_; ++offset) {
    hasher.update(p[offset]);
  }

  // TODO: try folly::small_vector?
  std::vector<segment_match<GranularityPolicy>> matches;
  const bool single_block_mode = cfg_.max_active_blocks == 1;

  auto total_bytes_read_before = prog_.total_bytes_read.load();
  prog_.current_offset.store(offset);
  prog_.current_size.store(size);

  while (offset < size) {
    ++stats_.bloom_lookups;
    if (filter_.test(hasher())) [[unlikely]] {
      ++stats_.bloom_hits;
      if (single_block_mode) {
        auto& block = blocks_.front();
        block.for_each_offset(
            hasher(), [&](auto off) { matches.emplace_back(&block, off); });
      } else {
        for (auto const& block : blocks_) {
          block.for_each_offset_filter(
              hasher(), [&](auto off) { matches.emplace_back(&block, off); });
        }
      }

      if (!matches.empty()) [[unlikely]] {
        ++stats_.bloom_true_positives;
        match_counts_.addValue(matches.size());

        LOG_TRACE << "found " << matches.size() << " matches (hash=" << hasher()
                  << ", window size=" << window_size_ << ")";

        for (auto& m : matches) {
          LOG_TRACE << "  block " << m.block_num() << " @ " << m.offset();
          m.verify_and_extend(p + offset - window_size_, window_size_,
                              p + written, p + size);
        }

        stats_.total_matches += matches.size();
        stats_.bad_matches +=
            std::count_if(matches.begin(), matches.end(),
                          [](auto const& m) { return m.size() == 0; });

        auto best = std::max_element(matches.begin(), matches.end());
        auto match_len = best->size();

        if (match_len > 0) {
          ++stats_.good_matches;
          LOG_TRACE << "successful match of length " << match_len << " @ "
                    << best->offset();

          auto block_num = best->block_num();
          auto match_off = best->offset();
          auto num_to_write = best->data() - (p + written);

          // best->block can be invalidated by this call to add_data()!
          add_data(chkable, written, num_to_write);
          written += num_to_write;
          finish_chunk(chkable);

          chkable.add_chunk(block_num, match_off, match_len);
          prog_.chunk_count++;
          written += match_len;

          prog_.saved_by_segmentation += match_len;

          offset = written;

          if (size - written < window_size_) {
            break;
          }

          hasher.clear();

          for (; offset < written + window_size_; ++offset) {
            hasher.update(p[offset]);
          }

          prog_.current_offset.store(offset);
          prog_.total_bytes_read.store(total_bytes_read_before + offset);

          next_hash_offset =
              written + lookback_size + blocks_.back().next_hash_distance();
        }

        matches.clear();

        if (match_len > 0) {
          continue;
        }
      }
    }

    // no matches found, see if we can append data
    // we need to keep at least lookback_size bytes unwritten

    if (offset == next_hash_offset) [[unlikely]] {
      auto num_to_write = offset - lookback_size - written;
      add_data(chkable, written, num_to_write);
      written += num_to_write;
      next_hash_offset += window_step_;
      prog_.current_offset.store(offset);
      prog_.total_bytes_read.store(total_bytes_read_before + offset);
    }

    hasher.update(p[offset - window_size_], p[offset]);
    ++offset;
  }

  prog_.current_offset.store(size);
  prog_.total_bytes_read.store(total_bytes_read_before + size);

  add_data(chkable, written, size - written);
  finish_chunk(chkable);
}

template <size_t N>
struct constant_granularity_segmenter_ {
  template <typename LoggerPolicy>
  using type = segmenter_<LoggerPolicy, ConstantGranularityPolicy<N>>;
};

struct variable_granularity_segmenter_ {
  template <typename LoggerPolicy>
  using type = segmenter_<LoggerPolicy, VariableGranularityPolicy>;
};

std::unique_ptr<segmenter::impl>
create_segmenter(logger& lgr, progress& prog,
                 std::shared_ptr<block_manager> blkmgr,
                 segmenter::config const& cfg,
                 compression_constraints const& cc,
                 segmenter::block_ready_cb block_ready) {
  if (!cc.granularity || cc.granularity.value() == 1) {
    return make_unique_logging_object<segmenter::impl,
                                      constant_granularity_segmenter_<1>::type,
                                      logger_policies>(
        lgr, prog, std::move(blkmgr), cfg, std::move(block_ready));
  }

  return make_unique_logging_object<
      segmenter::impl, variable_granularity_segmenter_::type, logger_policies>(
      lgr, prog, std::move(blkmgr), cfg, std::move(block_ready),
      cc.granularity.value());
}

} // namespace

segmenter::segmenter(logger& lgr, progress& prog,
                     std::shared_ptr<block_manager> blkmgr, config const& cfg,
                     compression_constraints const& cc,
                     block_ready_cb block_ready)
    : impl_(create_segmenter(lgr, prog, std::move(blkmgr), cfg, cc,
                             std::move(block_ready))) {}

} // namespace dwarfs
