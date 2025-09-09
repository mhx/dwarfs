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
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <algorithm>
#include <bit>
#include <cassert>
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
#include <folly/sorted_vector_types.h>
#include <folly/stats/Histogram.h>

#include <dwarfs/compiler.h>
#include <dwarfs/compression_constraints.h>
#include <dwarfs/error.h>
#include <dwarfs/logger.h>
#include <dwarfs/malloc_byte_buffer.h>
#include <dwarfs/util.h>
#include <dwarfs/writer/segmenter.h>
#include <dwarfs/writer/writer_progress.h>

#include <dwarfs/internal/malloc_buffer.h>
#include <dwarfs/writer/internal/block_manager.h>
#include <dwarfs/writer/internal/chunkable.h>
#include <dwarfs/writer/internal/cyclic_hash.h>
#include <dwarfs/writer/internal/entry.h>
#include <dwarfs/writer/internal/progress.h>

namespace dwarfs::writer {

namespace internal {

namespace {

/**
 * Segmenter Strategy
 *
 * For each *block*, start new rolling hash. The hashes are associated
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
 * a new hash values is computed.
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
  DWARFS_FORCE_INLINE void insert(KeyT const& key, ValT const& val) {
    if (!values_.insert(std::make_pair(key, val)).second) [[unlikely]] {
      collisions_[key].emplace_back(val);
    }
  }

  template <typename F>
  DWARFS_FORCE_INLINE void
  for_each_value(KeyT const& key, F const& func) const {
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

  template <typename F>
  DWARFS_FORCE_INLINE bool any_value_is(KeyT const& key, F const& func) const {
    if (auto it = values_.find(key); it != values_.end()) [[unlikely]] {
      if (func(it->second)) {
        return true;
      }
      if (auto it2 = collisions_.find(key); it2 != collisions_.end())
          [[unlikely]] {
        for (auto const& val : it2->second) {
          if (func(val)) {
            return true;
          }
        }
      }
    }
    return false;
  }

  DWARFS_FORCE_INLINE blockhash_t const& values() const { return values_; };
  DWARFS_FORCE_INLINE collision_t const& collisions() const {
    return collisions_;
  };

  size_t memory_usage() const {
    auto phmap_mem = []<typename T>(T const& m) {
      return m.capacity() * sizeof(typename T::slot_type) + m.capacity() + 1;
    };

    auto mem = phmap_mem(values_) + phmap_mem(collisions_);

    for (auto const& c : collisions_) {
      if (c.second.size() > MaxCollInline) {
        mem +=
            c.second.capacity() * sizeof(typename collision_vector::value_type);
      }
    }

    return mem;
  }

 private:
  blockhash_t values_;
  collision_t collisions_;
};

template <typename T, size_t MaxInline = 1>
using small_sorted_vector_set =
    folly::sorted_vector_set<T, std::less<T>, std::allocator<T>, void,
                             folly::small_vector<T, MaxInline>>;

using repeating_sequence_map_type =
    phmap::flat_hash_map<uint32_t, small_sorted_vector_set<uint8_t, 8>>;
using repeating_collisions_map_type = std::unordered_map<uint8_t, uint32_t>;

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

  static constexpr size_t const value_mask = 8 * sizeof(bits_type) - 1;
  static constexpr size_t const index_shift = std::popcount(value_mask);
  static constexpr size_t const alignment = 64;

  explicit bloom_filter(size_t size)
      : index_mask_{(std::max(size, value_mask + 1) >> index_shift) - 1}
      , size_{size > 0 ? std::max(size, value_mask + 1) : 0} {
    if (size > 0) {
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
  }

  ~bloom_filter() {
    if (bits_) {
      boost::alignment::aligned_free(bits_);
    }
  }

  DWARFS_FORCE_INLINE void add(size_t ix) {
    assert(bits_);
    auto bits = bits_;
    BOOST_ALIGN_ASSUME_ALIGNED(bits, sizeof(bits_type));
    bits[(ix >> index_shift) & index_mask_] |= static_cast<bits_type>(1)
                                               << (ix & value_mask);
  }

  DWARFS_FORCE_INLINE bool test(size_t ix) const {
    assert(bits_);
    auto bits = bits_;
    BOOST_ALIGN_ASSUME_ALIGNED(bits, sizeof(bits_type));
    return bits[(ix >> index_shift) & index_mask_] &
           (static_cast<bits_type>(1) << (ix & value_mask));
  }

  // size in bits
  DWARFS_FORCE_INLINE size_t size() const { return size_; }

  void clear() {
    assert(bits_);
    // NOLINTNEXTLINE(modernize-use-ranges)
    std::fill(begin(), end(), 0);
  }

  void merge(bloom_filter const& other) {
    assert(bits_);
    if (size() != other.size()) {
      throw std::runtime_error("size mismatch");
    }
    // NOLINTNEXTLINE(modernize-use-ranges)
    std::transform(cbegin(), cend(), other.cbegin(), begin(), std::bit_or<>{});
  }

  size_t memory_usage() const { return size_ / 8; }

 private:
  DWARFS_FORCE_INLINE bits_type const* cbegin() const { return bits_; }
  DWARFS_FORCE_INLINE bits_type const* cend() const {
    return bits_ + (size_ >> index_shift);
  }
  DWARFS_FORCE_INLINE bits_type* begin() { return bits_; }
  DWARFS_FORCE_INLINE bits_type* end() {
    return bits_ + (size_ >> index_shift);
  }

  bits_type* bits_{nullptr};
  size_t const index_mask_;
  size_t const size_;
};

/**
 * Granularity
 *
 * Segmenter granularity is needed because some compressors (e.g. FLAC or
 * other pcmaudio compressors) expect that their input data always starts
 * and ends with a full "frame", i.e. a complete set of samples for all
 * channels. So we must ensure we don't cut the data in the middle of a
 * frame (or even a sample) in the segmenter.
 *
 * The compressor will know the granularity from the metadata provided by
 * the categorizer and this granularity is passed to the segmenter.
 *
 * A granularity of 1 means we can cut the data as we see fit. A granularity
 * of e.g. 6 means we can only cut at offsets that are a multiple of 6.
 * It also means we need to e.g. truncate the block size accordingly if it
 * is not a multiple of the granularity.
 *
 * Algorithmically, we'll just pretend that the smallest unit of data is
 * `granularity` Bytes. That means, if our window size is 1024 and the
 * granularity is 6, the window will be 6*1024 bytes wide.
 *
 * Because we don't want to sacrifice performance for the most common
 * case (granularity == 1), we use two policies: a constant granularity
 * policy, which at N == 1 represents granularity == 1, and a variable
 * granularity policy. The constant granularity policy should compile
 * down to much more efficient code as it avoids a lot of run-time checks.
 */

class GranularityPolicyBase {
 public:
  static std::string chunkable_size_fail_message(auto size, auto granularity) {
    return fmt::format(
        "unexpected size {} for given granularity {} (modulus: {})", size,
        granularity, size % granularity);
  }
};

template <size_t N>
class ConstantGranularityPolicy : private GranularityPolicyBase {
 public:
  static constexpr size_t const kGranularity{N};

  template <typename T>
  static void add_new_block(T& blocks, logger& lgr,
                            repeating_sequence_map_type const& repseqmap,
                            repeating_collisions_map_type& repcoll, size_t num,
                            size_t size, size_t window_size, size_t window_step,
                            size_t bloom_filter_size) {
    blocks.emplace_back(lgr, repseqmap, repcoll, num, size, window_size,
                        window_step, bloom_filter_size);
  }

  template <typename T, typename U>
  static DWARFS_FORCE_INLINE void
  add_match(T& matches, U const* block, uint32_t off) {
    matches.emplace_back(block, off);
  }

  static DWARFS_FORCE_INLINE bool is_valid_granularity_size(auto size) {
    if constexpr (kGranularity > 1) {
      return size % kGranularity == 0;
    } else {
      return true;
    }
  }

  static DWARFS_FORCE_INLINE void check_chunkable_size(auto size) {
    if constexpr (kGranularity > 1) {
      DWARFS_CHECK(is_valid_granularity_size(size),
                   chunkable_size_fail_message(size, kGranularity));
    }
  }

  static DWARFS_FORCE_INLINE size_t constrained_block_size(size_t size) {
    if constexpr (kGranularity > 1) {
      size -= size % kGranularity;
    }
    return size;
  }

  template <typename T, typename... Args>
  static DWARFS_FORCE_INLINE T create(Args&&... args) {
    return T(std::forward<Args>(args)...);
  }

  static DWARFS_FORCE_INLINE size_t bytes_to_frames(size_t size) {
    assert(size % kGranularity == 0);
    return size / kGranularity;
  }

  static DWARFS_FORCE_INLINE size_t frames_to_bytes(size_t size) {
    return size * kGranularity;
  }

  template <typename T>
  static DWARFS_FORCE_INLINE void for_bytes_in_frame(T const& func) {
    for (size_t i = 0; i < kGranularity; ++i) {
      func();
    }
  }

  static DWARFS_FORCE_INLINE uint_fast32_t granularity_bytes() {
    return kGranularity;
  }

  static DWARFS_FORCE_INLINE bool compile_time_granularity() { return true; }
};

class VariableGranularityPolicy : private GranularityPolicyBase {
 public:
  explicit DWARFS_FORCE_INLINE
  VariableGranularityPolicy(uint32_t granularity) noexcept
      : granularity_{granularity} {}

  template <typename T>
  void add_new_block(T& blocks, logger& lgr,
                     repeating_sequence_map_type const& repseqmap,
                     repeating_collisions_map_type& repcoll, size_t num,
                     size_t size, size_t window_size, size_t window_step,
                     size_t bloom_filter_size) const {
    blocks.emplace_back(lgr, repseqmap, repcoll, num, size, window_size,
                        window_step, bloom_filter_size, granularity_);
  }

  template <typename T, typename U>
  DWARFS_FORCE_INLINE void
  add_match(T& matches, U const* block, uint32_t off) const {
    matches.emplace_back(block, off, granularity_);
  }

  DWARFS_FORCE_INLINE bool is_valid_granularity_size(auto size) const {
    return size % granularity_ == 0;
  }

  DWARFS_FORCE_INLINE void check_chunkable_size(auto size) const {
    if (granularity_ > 1) {
      DWARFS_CHECK(is_valid_granularity_size(size),
                   chunkable_size_fail_message(size, granularity_));
    }
  }

  DWARFS_FORCE_INLINE size_t constrained_block_size(size_t size) const {
    if (granularity_ > 1) {
      size -= size % granularity_;
    }
    return size;
  }

  template <typename T, typename... Args>
  DWARFS_FORCE_INLINE T create(Args&&... args) const {
    return T(std::forward<Args>(args)..., granularity_);
  }

  DWARFS_FORCE_INLINE size_t bytes_to_frames(size_t size) const {
    assert(size % granularity_ == 0);
    return size / granularity_;
  }

  DWARFS_FORCE_INLINE size_t frames_to_bytes(size_t size) const {
    return size * granularity_;
  }

  template <typename T>
  DWARFS_FORCE_INLINE void for_bytes_in_frame(T const& func) const {
    for (size_t i = 0; i < granularity_; ++i) {
      func();
    }
  }

  DWARFS_FORCE_INLINE uint_fast32_t granularity_bytes() const {
    return granularity_;
  }

  static DWARFS_FORCE_INLINE bool compile_time_granularity() { return false; }

 private:
  uint_fast32_t const granularity_;
};

template <typename T, typename GranularityPolicy>
class granular_span_adapter : private GranularityPolicy {
 public:
  static_assert(sizeof(T) == 1, "T must be a byte type (for now)");

  template <typename... PolicyArgs>
  DWARFS_FORCE_INLINE
  granular_span_adapter(std::span<T> s, PolicyArgs&&... args)
      : GranularityPolicy(std::forward<PolicyArgs>(args)...)
      , s_{s} {}

  DWARFS_FORCE_INLINE size_t size() const {
    return this->bytes_to_frames(s_.size());
  }

  DWARFS_FORCE_INLINE size_t size_in_bytes() const { return s_.size(); }

  template <typename H>
  DWARFS_FORCE_INLINE void update_hash(H& hasher, size_t offset) const {
    offset = this->frames_to_bytes(offset);
    this->for_bytes_in_frame([&] { hasher.update(s_[offset++]); });
  }

  template <typename H>
  DWARFS_FORCE_INLINE void
  update_hash(H& hasher, size_t from, size_t to) const {
    from = this->frames_to_bytes(from);
    to = this->frames_to_bytes(to);
    this->for_bytes_in_frame([&] { hasher.update(s_[from++], s_[to++]); });
  }

  DWARFS_FORCE_INLINE void append_to(auto& v) const {
    v.append(s_.data(), s_.size());
  }

  DWARFS_FORCE_INLINE int compare(size_t offset, std::span<T const> rhs) const {
    auto const offset_in_bytes = this->frames_to_bytes(offset);
    assert(offset_in_bytes + rhs.size() <= s_.size());
    return std::memcmp(s_.data() + offset_in_bytes, rhs.data(), rhs.size());
  }

 private:
  std::span<T> s_;
};

template <typename GranularityPolicy, bool SegmentationEnabled, bool MultiBlock>
class BasicSegmentationPolicy : public GranularityPolicy {
 public:
  using GranularityPolicyT = GranularityPolicy;

  template <typename... PolicyArgs>
  DWARFS_FORCE_INLINE BasicSegmentationPolicy(PolicyArgs&&... args)
      : GranularityPolicy(std::forward<PolicyArgs>(args)...) {}

  static DWARFS_FORCE_INLINE constexpr bool is_segmentation_enabled() {
    return SegmentationEnabled;
  }

  static DWARFS_FORCE_INLINE constexpr bool is_multi_block_mode() {
    return MultiBlock;
  }
};

template <typename GranularityPolicy>
using SegmentationDisabledPolicy =
    BasicSegmentationPolicy<GranularityPolicy, false, false>;

template <typename GranularityPolicy>
using SingleBlockSegmentationPolicy =
    BasicSegmentationPolicy<GranularityPolicy, true, false>;

template <typename GranularityPolicy>
using MultiBlockSegmentationPolicy =
    BasicSegmentationPolicy<GranularityPolicy, true, true>;

template <typename T, typename GranularityPolicy>
class basic_granular_container_adapter : private GranularityPolicy {
 public:
  using value_type = typename T::value_type;
  static_assert(sizeof(value_type) == 1,
                "value_type must be a byte type (for now)");

  template <typename... PolicyArgs>
  DWARFS_FORCE_INLINE
  basic_granular_container_adapter(T& v, PolicyArgs&&... args)
      : GranularityPolicy(std::forward<PolicyArgs>(args)...)
      , v_{v} {}

  DWARFS_FORCE_INLINE size_t size() const {
    return this->bytes_to_frames(v_.size());
  }

  DWARFS_FORCE_INLINE void append(
      granular_span_adapter<value_type const, GranularityPolicy> const& span) {
    span.append_to(v_);
  }

  DWARFS_FORCE_INLINE std::span<value_type const>
  subspan(size_t offset, size_t size) const {
    auto const offset_in_bytes = this->frames_to_bytes(offset);
    auto const size_in_bytes = this->frames_to_bytes(size);
    assert(offset_in_bytes + size_in_bytes <= v_.size());
    return std::span<value_type const>(v_.data() + offset_in_bytes,
                                       size_in_bytes);
  }

  template <typename H>
  DWARFS_FORCE_INLINE void update_hash(H& hasher, size_t offset) const {
    auto p = v_.data() + this->frames_to_bytes(offset);
    this->for_bytes_in_frame([&] { hasher.update(*p++); });
  }

  template <typename H>
  DWARFS_FORCE_INLINE void
  update_hash(H& hasher, size_t from, size_t to) const {
    auto const p = v_.data();
    auto pfrom = p + this->frames_to_bytes(from);
    auto pto = p + this->frames_to_bytes(to);
    this->for_bytes_in_frame([&] { hasher.update(*pfrom++, *pto++); });
  }

 private:
  T& v_;
};

template <typename GranularityPolicy>
using granular_buffer_adapter =
    basic_granular_container_adapter<dwarfs::internal::malloc_buffer,
                                     GranularityPolicy>;

template <typename LoggerPolicy, typename GranularityPolicy>
class active_block : private GranularityPolicy {
 private:
  using GranularityPolicy::bytes_to_frames;
  using GranularityPolicy::frames_to_bytes;
  using offset_t = uint32_t;
  using hash_t = uint32_t;

 public:
  template <typename... PolicyArgs>
  active_block(logger& lgr, repeating_sequence_map_type const& repseqmap,
               repeating_collisions_map_type& repcoll, size_t num,
               size_t size_in_frames, size_t window_size, size_t window_step,
               size_t bloom_filter_size, PolicyArgs&&... args)
      : GranularityPolicy(std::forward<PolicyArgs>(args)...)
      , LOG_PROXY_INIT(lgr)
      , num_(num)
      , capacity_in_frames_(size_in_frames)
      , window_size_(window_size)
      , window_step_mask_(window_step - 1)
      , filter_(bloom_filter_size)
      , repseqmap_{repseqmap}
      , repeating_collisions_{repcoll}
      , data_{malloc_byte_buffer::create()} {
    DWARFS_CHECK((window_step & window_step_mask_) == 0,
                 "window step size not a power of two");
    data_.reserve(this->frames_to_bytes(capacity_in_frames_));
  }

  ~active_block() {
    LOG_DEBUG << "block " << num_ << " destroyed, "
              << size_with_unit(offsets_.memory_usage()) << " offset memory ("
              << fmt::format("{:.1f}",
                             static_cast<double>(offsets_.memory_usage()) /
                                 offsets_.values().size())
              << " bytes per offset, " << offsets_.values().size()
              << " offsets, " << offsets_.collisions().size()
              << " collisions), " << size_with_unit(filter_.memory_usage())
              << " bloom filter";
  }

  DWARFS_FORCE_INLINE size_t num() const { return num_; }

  DWARFS_FORCE_INLINE size_t size_in_frames() const {
    return this->bytes_to_frames(data_.size());
  }

  DWARFS_FORCE_INLINE bool full() const {
    return size_in_frames() == capacity_in_frames_;
  }

  DWARFS_FORCE_INLINE mutable_byte_buffer data() const { return data_; }

  DWARFS_FORCE_INLINE void
  append_bytes(std::span<uint8_t const> data, bloom_filter& global_filter);

  DWARFS_FORCE_INLINE size_t next_hash_distance_in_frames() const {
    return window_step_mask_ + 1 - (size_in_frames() & window_step_mask_);
  }

  template <typename F>
  DWARFS_FORCE_INLINE void for_each_offset(hash_t key, F&& func) const {
    offsets_.for_each_value(key, std::forward<F>(func));
  }

  template <typename F>
  DWARFS_FORCE_INLINE void for_each_offset_filter(hash_t key, F&& func) const {
    if (filter_.test(key)) [[unlikely]] {
      offsets_.for_each_value(key, std::forward<F>(func));
    }
  }

  DWARFS_FORCE_INLINE void finalize(segmenter_stats& stats) {
    stats.total_hashes += offsets_.values().size();
    for (auto& c : offsets_.collisions()) {
      stats.total_hashes += c.second.size();
      stats.l2_collisions += c.second.size() - 1;
      stats.l2_collision_vec_size.addValue(c.second.size());
    }
  }

  DWARFS_FORCE_INLINE bloom_filter const& filter() const { return filter_; }

 private:
  DWARFS_FORCE_INLINE bool
  is_existing_repeating_sequence(hash_t hashval, size_t offset);

  static constexpr size_t num_inline_offsets = 4;

  LOG_PROXY_DECL(LoggerPolicy);
  size_t const num_, capacity_in_frames_, window_size_, window_step_mask_;
  rsync_hash hasher_;
  bloom_filter filter_;
  fast_multimap<hash_t, offset_t, num_inline_offsets> offsets_;
  repeating_sequence_map_type const& repseqmap_;
  repeating_collisions_map_type& repeating_collisions_;
  mutable_byte_buffer data_;
};

class segmenter_progress : public progress::context {
 public:
  using status = progress::context::status;

  segmenter_progress(std::string context, size_t total_size)
      : context_{std::move(context)}
      , bytes_total_{total_size} {}

  status get_status() const override {
    auto f = current_file.load();
    status st;
    st.color = termcolor::GREEN;
    st.context = context_;
    if (f) {
      st.path.emplace(f->path_as_string());
    }
    st.bytes_processed.emplace(bytes_processed.load());
    st.bytes_total.emplace(bytes_total_);
    return st;
  }

  // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes)
  std::atomic<file const*> current_file{nullptr};
  std::atomic<size_t> bytes_processed{0};
  // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes)

 private:
  std::string const context_;
  size_t const bytes_total_;
};

DWARFS_FORCE_INLINE size_t window_size(segmenter::config const& cfg) {
  return cfg.blockhash_window_size > 0
             ? static_cast<size_t>(1) << cfg.blockhash_window_size
             : 0;
}

DWARFS_FORCE_INLINE size_t window_step(segmenter::config const& cfg) {
  return std::max<size_t>(1, window_size(cfg) >> cfg.window_increment_shift);
}

template <typename LoggerPolicy, typename SegmentingPolicy>
class segmenter_ final : public segmenter::impl, private SegmentingPolicy {
 private:
  using GranularityPolicyT = typename SegmentingPolicy::GranularityPolicyT;
  using GranularityPolicyT::add_match;
  using GranularityPolicyT::add_new_block;
  using GranularityPolicyT::bytes_to_frames;
  using GranularityPolicyT::compile_time_granularity;
  using GranularityPolicyT::constrained_block_size;
  using GranularityPolicyT::frames_to_bytes;
  using GranularityPolicyT::granularity_bytes;
  using SegmentingPolicy::is_multi_block_mode;
  using SegmentingPolicy::is_segmentation_enabled;

 public:
  template <typename... PolicyArgs>
  segmenter_(logger& lgr, progress& prog, std::shared_ptr<block_manager> blkmgr,
             segmenter::config const& cfg, size_t total_size,
             segmenter::block_ready_cb block_ready, PolicyArgs&&... args)
      : SegmentingPolicy(std::forward<PolicyArgs>(args)...)
      , LOG_PROXY_INIT(lgr)
      , prog_{prog}
      , blkmgr_{std::move(blkmgr)}
      , cfg_{cfg}
      , block_ready_{std::move(block_ready)}
      , pctx_{prog.create_context<segmenter_progress>(cfg.context, total_size)}
      , window_size_{window_size(cfg)}
      , window_step_{window_step(cfg)}
      , block_size_in_frames_{block_size_in_frames(cfg)}
      , global_filter_{bloom_filter_size(cfg)}
      , match_counts_{1, 0, 128} {
    if constexpr (is_segmentation_enabled()) {
      LOG_VERBOSE << cfg_.context << "using a "
                  << size_with_unit(frames_to_bytes(window_size_))
                  << " window at "
                  << size_with_unit(frames_to_bytes(window_step_))
                  << " steps with "
                  << (compile_time_granularity() ? "compile" : "run")
                  << "-time " << granularity_bytes()
                  << "-byte frames for segment analysis";
      LOG_VERBOSE << cfg_.context << "bloom filter size: "
                  << size_with_unit(global_filter_.size() / 8);

      for (int i = 0; i < 256; ++i) {
        auto val =
            rsync_hash::repeating_window(i, frames_to_bytes(window_size_));
        DWARFS_CHECK(repeating_sequence_hash_values_[val].emplace(i).second,
                     "repeating sequence hash value / byte collision");
      }
    }
  }

  void add_chunkable(chunkable& chkable) override;
  void finish() override;

 private:
  struct chunk_state {
    size_t offset_in_frames{0};
    size_t size_in_frames{0};
  };

  DWARFS_FORCE_INLINE void block_ready();
  void finish_chunk(chunkable& chkable);
  DWARFS_FORCE_INLINE void
  append_to_block(chunkable& chkable, size_t offset_in_frames,
                  size_t size_in_frames);
  void
  add_data(chunkable& chkable, size_t offset_in_frames, size_t size_in_frames);
  DWARFS_FORCE_INLINE void
  segment_and_add_data(chunkable& chkable, size_t size_in_frames);

  DWARFS_FORCE_INLINE size_t
  bloom_filter_size(segmenter::config const& cfg) const {
    if constexpr (is_segmentation_enabled()) {
      auto hash_count =
          std::bit_ceil(std::max<size_t>(1, cfg.max_active_blocks) *
                        (block_size_in_frames(cfg) / window_step(cfg)));
      return (static_cast<size_t>(1) << cfg.bloom_filter_size) * hash_count;
    }

    return 0;
  }

  size_t DWARFS_FORCE_INLINE
  block_size_in_frames(segmenter::config const& cfg) const {
    auto raw_size = static_cast<size_t>(1) << cfg.block_size_bits;
    return bytes_to_frames(constrained_block_size(raw_size));
  }

  LOG_PROXY_DECL(LoggerPolicy);
  progress& prog_;
  std::shared_ptr<block_manager> blkmgr_;
  segmenter::config const cfg_;
  segmenter::block_ready_cb block_ready_;
  std::shared_ptr<segmenter_progress> pctx_;

  size_t const window_size_;
  size_t const window_step_;
  size_t const block_size_in_frames_;

  chunk_state chunk_;

  bloom_filter global_filter_;

  segmenter_stats stats_;

  using active_block_type = active_block<LoggerPolicy, GranularityPolicyT>;

  // Active blocks are blocks that can still be referenced from new chunks.
  // Up to N blocks (configurable) can be active and are kept in this queue.
  // All active blocks except for the last one are immutable and potentially
  // already being compressed.
  std::deque<active_block_type> blocks_;

  repeating_sequence_map_type repeating_sequence_hash_values_;
  repeating_collisions_map_type repeating_collisions_;

  folly::Histogram<size_t> match_counts_;
};

template <typename LoggerPolicy, typename GranularityPolicy>
class segment_match : private GranularityPolicy {
 public:
  using active_block_type = active_block<LoggerPolicy, GranularityPolicy>;

  template <typename... PolicyArgs>
  DWARFS_FORCE_INLINE segment_match(active_block_type const* blk, uint32_t off,
                                    PolicyArgs&&... args) noexcept
      : GranularityPolicy(std::forward<PolicyArgs>(args)...)
      , block_{blk}
      , offset_{off} {}

  void verify_and_extend(
      granular_span_adapter<uint8_t const, GranularityPolicy> const& data,
      size_t pos, size_t len, size_t begin, size_t end);

  DWARFS_FORCE_INLINE bool operator<(segment_match const& rhs) const {
    return size_ < rhs.size_ ||
           (size_ == rhs.size_ &&
            (block_->num() < rhs.block_->num() ||
             (block_->num() == rhs.block_->num() && offset_ < rhs.offset_)));
  }

  DWARFS_FORCE_INLINE size_t pos() const { return pos_; }
  DWARFS_FORCE_INLINE uint32_t size() const { return size_; }
  DWARFS_FORCE_INLINE uint32_t offset() const { return offset_; }
  DWARFS_FORCE_INLINE size_t block_num() const { return block_->num(); }

 private:
  active_block_type const* block_;
  uint32_t offset_;
  uint32_t size_{0};
  size_t pos_{0};
};

template <typename LoggerPolicy, typename GranularityPolicy>
DWARFS_FORCE_INLINE bool
active_block<LoggerPolicy, GranularityPolicy>::is_existing_repeating_sequence(
    hash_t hashval, size_t offset) {
  if (auto it = repseqmap_.find(hashval); it != repseqmap_.end()) [[unlikely]] {
    auto pdata = data_.data();
    auto winbeg = pdata + frames_to_bytes(offset);
    auto winend = winbeg + frames_to_bytes(window_size_);
    auto byte = *winbeg;
    static_assert(std::is_same_v<typename decltype(it->second)::value_type,
                                 decltype(byte)>);

    // check if this is a known character for a repeating sequence
    if (!it->second.contains(byte)) {
      return false;
    }

    if (std::find_if(winbeg, winend, [byte](auto b) { return b != byte; }) ==
        winend) {
      return offsets_.any_value_is(hashval, [&, this](auto off) {
        auto offbeg = pdata + frames_to_bytes(off);
        auto offend = offbeg + frames_to_bytes(window_size_);

        if (std::find_if(offbeg, offend,
                         [byte](auto b) { return b != byte; }) == offend) {
          ++repeating_collisions_[byte];
          return true;
        }

        return false;
      });
    }
  }

  return false;
}

template <typename LoggerPolicy, typename GranularityPolicy>
DWARFS_FORCE_INLINE void
active_block<LoggerPolicy, GranularityPolicy>::append_bytes(
    std::span<uint8_t const> data, bloom_filter& global_filter) {
  auto src = this->template create<
      granular_span_adapter<uint8_t const, GranularityPolicy>>(data);

  auto v = this->template create<granular_buffer_adapter<GranularityPolicy>>(
      data_.raw_buffer());

  // TODO: this works in theory, but slows down the segmenter by almost 10%
  // auto v = this->template create<
  //     granular_byte_buffer_adapter<GranularityPolicy>>(data_);

  auto offset = v.size();

  DWARFS_CHECK(offset + src.size() <= capacity_in_frames_,
               fmt::format("block capacity exceeded: {} + {} > {}",
                           frames_to_bytes(offset), frames_to_bytes(src.size()),
                           frames_to_bytes(capacity_in_frames_)));

  v.append(src);

  if (window_size_ > 0) {
    while (offset < v.size()) {
      if (offset < window_size_) [[unlikely]] {
        v.update_hash(hasher_, offset);
      } else {
        v.update_hash(hasher_, offset - window_size_, offset);
      }
      if (++offset >= window_size_) [[likely]] {
        if ((offset & window_step_mask_) == 0) [[unlikely]] {
          auto hashval = hasher_();
          if (!is_existing_repeating_sequence(hashval, offset - window_size_))
              [[likely]] {
            offsets_.insert(hashval, offset - window_size_);
            if (filter_.size() > 0) {
              filter_.add(hashval);
            }
            global_filter.add(hashval);
          }
        }
      }
    }
  }
}

template <typename LoggerPolicy, typename GranularityPolicy>
void segment_match<LoggerPolicy, GranularityPolicy>::verify_and_extend(
    granular_span_adapter<uint8_t const, GranularityPolicy> const& data,
    size_t pos, size_t len, size_t begin, size_t end) {
  auto v = this->template create<granular_buffer_adapter<GranularityPolicy>>(
      block_->data().raw_buffer());

  // First, check if the regions actually match
  if (data.compare(pos, v.subspan(offset_, len)) == 0) {
    // scan backward
    auto tmp = offset_;
    while (tmp > 0 && pos > begin &&
           data.compare(pos - 1, v.subspan(tmp - 1, 1)) == 0) {
      --tmp;
      --pos;
    }
    len += offset_ - tmp;
    offset_ = tmp;
    pos_ = pos;

    // scan forward
    pos += len;
    tmp = offset_ + len;
    while (tmp < v.size() && pos < end &&
           data.compare(pos, v.subspan(tmp, 1)) == 0) {
      ++tmp;
      ++pos;
    }
    size_ = tmp - offset_;
  }

  // No match, this was a hash collision, we're done.
  // size_ defaults to 0 unless we have a real match and set it above.
}

template <typename LoggerPolicy, typename SegmentingPolicy>
void segmenter_<LoggerPolicy, SegmentingPolicy>::add_chunkable(
    chunkable& chkable) {
  if (auto size_in_frames = bytes_to_frames(chkable.size());
      size_in_frames > 0) {
    LOG_TRACE << cfg_.context << "adding " << chkable.description();

    pctx_->current_file = chkable.get_file();

    if (!is_segmentation_enabled() or size_in_frames < window_size_) {
      // no point dealing with hashing, just write it out
      add_data(chkable, 0, size_in_frames);
      finish_chunk(chkable);
      prog_.total_bytes_read += chkable.size();
      pctx_->bytes_processed += chkable.size();
    } else {
      segment_and_add_data(chkable, size_in_frames);
    }
  }
}

template <typename LoggerPolicy, typename SegmentingPolicy>
void segmenter_<LoggerPolicy, SegmentingPolicy>::finish() {
  if (!blocks_.empty() && !blocks_.back().full()) {
    block_ready();
  }

  auto l1_collisions = stats_.l2_collision_vec_size.computeTotalCount();

  if (stats_.bloom_lookups > 0) {
    LOG_VERBOSE << cfg_.context << "bloom filter reject rate: "
                << fmt::format("{:.3f}%", 100.0 - 100.0 * stats_.bloom_hits /
                                                      stats_.bloom_lookups)
                << " (TPR="
                << fmt::format("{:.3f}%", 100.0 * stats_.bloom_true_positives /
                                              stats_.bloom_hits)
                << ", lookups=" << stats_.bloom_lookups << ")";
  }
  if (stats_.total_matches > 0) {
    LOG_VERBOSE << fmt::format(
        "{}segment matches: good={}, bad={}, collisions={}, total={}",
        cfg_.context, stats_.good_matches, stats_.bad_matches,
        (stats_.total_matches - (stats_.bad_matches + stats_.good_matches)),
        stats_.total_matches);
  }
  if (stats_.total_hashes > 0) {
    LOG_VERBOSE << cfg_.context << "segmentation collisions: L1="
                << fmt::format("{:.3f}%",
                               100.0 * (l1_collisions + stats_.l2_collisions) /
                                   stats_.total_hashes)
                << ", L2="
                << fmt::format("{:.3f}%", 100.0 * stats_.l2_collisions /
                                              stats_.total_hashes)
                << " [" << stats_.total_hashes << " hashes]";
  }

  if (l1_collisions > 0) {
    auto pct = [&](double p) {
      return stats_.l2_collision_vec_size.getPercentileEstimate(p);
    };
    LOG_VERBOSE << cfg_.context << "collision vector size p50: " << pct(0.5)
                << ", p75: " << pct(0.75) << ", p90: " << pct(0.9)
                << ", p95: " << pct(0.95) << ", p99: " << pct(0.99);
  }

  auto pct = [&](double p) { return match_counts_.getPercentileEstimate(p); };

  LOG_VERBOSE << cfg_.context << "match counts p50: " << pct(0.5)
              << ", p75: " << pct(0.75) << ", p90: " << pct(0.9)
              << ", p95: " << pct(0.95) << ", p99: " << pct(0.99);

  for (auto [k, v] : repeating_collisions_) {
    LOG_VERBOSE << cfg_.context
                << fmt::format(
                       "avoided {} collisions in 0x{:02x}-byte sequences", v,
                       k);
  }
}

template <typename LoggerPolicy, typename SegmentingPolicy>
DWARFS_FORCE_INLINE void
segmenter_<LoggerPolicy, SegmentingPolicy>::block_ready() {
  auto& block = blocks_.back();
  block.finalize(stats_);
  block_ready_(block.data().share(), block.num());
  ++prog_.block_count;
}

template <typename LoggerPolicy, typename SegmentingPolicy>
DWARFS_FORCE_INLINE void
segmenter_<LoggerPolicy, SegmentingPolicy>::append_to_block(
    chunkable& chkable, size_t offset_in_frames, size_t size_in_frames) {
  if (blocks_.empty() or blocks_.back().full()) [[unlikely]] {
    if (blocks_.size() >= std::max<size_t>(1, cfg_.max_active_blocks)) {
      blocks_.pop_front();
    }

    if constexpr (is_segmentation_enabled()) {
      global_filter_.clear();
      if constexpr (is_multi_block_mode()) {
        for (auto const& b : blocks_) {
          global_filter_.merge(b.filter());
        }
      }
    }

    add_new_block(blocks_, LOG_GET_LOGGER, repeating_sequence_hash_values_,
                  repeating_collisions_, blkmgr_->get_logical_block(),
                  block_size_in_frames_,
                  cfg_.max_active_blocks > 0 ? window_size_ : 0, window_step_,
                  is_multi_block_mode() ? global_filter_.size() : 0);
  }

  auto const offset_in_bytes = frames_to_bytes(offset_in_frames);
  auto const size_in_bytes = frames_to_bytes(size_in_frames);
  auto& block = blocks_.back();

  LOG_TRACE << cfg_.context << "appending " << size_in_bytes
            << " bytes to block " << block.num() << " @ "
            << frames_to_bytes(block.size_in_frames())
            << " from chunkable offset " << offset_in_bytes;

  block.append_bytes(chkable.span().subspan(offset_in_bytes, size_in_bytes),
                     global_filter_);
  chunk_.size_in_frames += size_in_frames;

  prog_.filesystem_size += size_in_bytes;

  if (block.full()) [[unlikely]] {
    if (auto ec = chkable.release_until(offset_in_bytes + size_in_bytes)) {
      LOG_DEBUG << cfg_.context
                << "error releasing chunkable: " << ec.message();
    }
    finish_chunk(chkable);
    block_ready();
  }
}

template <typename LoggerPolicy, typename SegmentingPolicy>
void segmenter_<LoggerPolicy, SegmentingPolicy>::add_data(
    chunkable& chkable, size_t offset_in_frames, size_t size_in_frames) {
  while (size_in_frames > 0) {
    size_t block_offset_in_frames = 0;

    if (!blocks_.empty()) {
      block_offset_in_frames = blocks_.back().size_in_frames();
    }

    size_t chunk_size_in_frames = std::min(
        size_in_frames, block_size_in_frames_ - block_offset_in_frames);

    append_to_block(chkable, offset_in_frames, chunk_size_in_frames);

    offset_in_frames += chunk_size_in_frames;
    size_in_frames -= chunk_size_in_frames;
  }
}

template <typename LoggerPolicy, typename SegmentingPolicy>
void segmenter_<LoggerPolicy, SegmentingPolicy>::finish_chunk(
    chunkable& chkable) {
  if (chunk_.size_in_frames > 0) {
    auto& block = blocks_.back();
    chkable.add_chunk(block.num(), frames_to_bytes(chunk_.offset_in_frames),
                      frames_to_bytes(chunk_.size_in_frames));
    chunk_.offset_in_frames = block.full() ? 0 : block.size_in_frames();
    chunk_.size_in_frames = 0;
    prog_.chunk_count++;
  }
}

template <typename LoggerPolicy, typename SegmentingPolicy>
DWARFS_FORCE_INLINE void
segmenter_<LoggerPolicy, SegmentingPolicy>::segment_and_add_data(
    chunkable& chkable, size_t size_in_frames) {
  rsync_hash hasher;
  size_t offset_in_frames = 0;
  size_t frames_written = 0;
  size_t lookback_size_in_frames = window_size_ + window_step_;
  size_t next_hash_offset_in_frames =
      lookback_size_in_frames +
      (blocks_.empty() ? window_step_
                       : blocks_.back().next_hash_distance_in_frames());
  auto data = this->template create<
      granular_span_adapter<uint8_t const, GranularityPolicyT>>(chkable.span());

  DWARFS_CHECK(size_in_frames >= window_size_,
               "unexpected call to segment_and_add_data");

  for (; offset_in_frames < window_size_; ++offset_in_frames) {
    data.update_hash(hasher, offset_in_frames);
  }

  folly::small_vector<segment_match<LoggerPolicy, GranularityPolicyT>, 1>
      matches;

  // // TODO: we have multiple segmenter threads, so this doesn't fly anymore
  // auto total_bytes_read_before = prog_.total_bytes_read.load();
  // prog_.current_offset.store(
  //     frames_to_bytes(offset_in_frames)); // TODO: what do we do with this?
  // prog_.current_size.store(frames_to_bytes(size_in_frames)); // TODO

  // TODO: how can we reasonably update the top progress bar with
  //       multiple concurrent segmenters?

  auto update_progress =
      [this, last_offset = static_cast<size_t>(0)](size_t offset) mutable {
        auto bytes = frames_to_bytes(offset - last_offset);
        prog_.total_bytes_read += bytes;
        pctx_->bytes_processed += bytes;
        last_offset = offset;
      };

  while (offset_in_frames < size_in_frames) {
    ++stats_.bloom_lookups;

    if (global_filter_.test(hasher())) [[unlikely]] {
      ++stats_.bloom_hits;

      if constexpr (is_multi_block_mode()) {
        for (auto const& block : blocks_) {
          block.for_each_offset_filter(hasher(), [&, this](auto off) {
            this->add_match(matches, &block, off);
          });
        }
      } else {
        auto& block = blocks_.front();
        block.for_each_offset(hasher(), [&, this](auto off) {
          this->add_match(matches, &block, off);
        });
      }

      if (!matches.empty()) [[unlikely]] {
        ++stats_.bloom_true_positives;
        match_counts_.addValue(matches.size());

        LOG_TRACE << cfg_.context << "[" << blocks_.back().num() << " @ "
                  << frames_to_bytes(blocks_.back().size_in_frames())
                  << ", chunkable @ " << frames_to_bytes(offset_in_frames)
                  << "] found " << matches.size()
                  << " matches (hash=" << fmt::format("{:08x}", hasher())
                  << ", window size=" << window_size_ << ")";

        for (auto& m : matches) {
          LOG_TRACE << cfg_.context << "  block " << m.block_num() << " @ "
                    << m.offset();

          m.verify_and_extend(data, offset_in_frames - window_size_,
                              window_size_, frames_written, size_in_frames);

          LOG_TRACE << cfg_.context << "    -> " << m.offset() << " -> "
                    << m.size();
        }

        stats_.total_matches += matches.size();
        stats_.bad_matches +=
            std::count_if(matches.begin(), matches.end(),
                          [](auto const& m) { return m.size() == 0; });

        auto best = std::max_element(matches.begin(), matches.end());
        auto match_len = best->size();

        if (match_len > 0) {
          ++stats_.good_matches;
          LOG_TRACE << cfg_.context << "successful match of length "
                    << match_len << " @ " << best->offset();

          auto block_num = best->block_num();
          auto match_off = best->offset();
          auto num_to_write = best->pos() - frames_written;

          // best->block can be invalidated by this call to add_data()!
          add_data(chkable, frames_written, num_to_write);
          frames_written += num_to_write;
          finish_chunk(chkable);

          chkable.add_chunk(block_num, frames_to_bytes(match_off),
                            frames_to_bytes(match_len));

          prog_.chunk_count++;
          frames_written += match_len;

          prog_.saved_by_segmentation += frames_to_bytes(match_len);

          offset_in_frames = frames_written;

          if (size_in_frames - frames_written < window_size_) {
            break;
          }

          hasher.clear();

          for (; offset_in_frames < frames_written + window_size_;
               ++offset_in_frames) {
            data.update_hash(hasher, offset_in_frames);
          }

          update_progress(offset_in_frames);

          next_hash_offset_in_frames =
              frames_written + lookback_size_in_frames +
              blocks_.back().next_hash_distance_in_frames();
        }

        matches.clear();

        if (match_len > 0) {
          continue;
        }
      }
    }

    // no matches found, see if we can append data
    // we need to keep at least lookback_size_in_frames frames unwritten

    if (offset_in_frames == next_hash_offset_in_frames) [[unlikely]] {
      auto num_to_write =
          offset_in_frames - lookback_size_in_frames - frames_written;
      add_data(chkable, frames_written, num_to_write);
      frames_written += num_to_write;
      next_hash_offset_in_frames += window_step_;

      update_progress(offset_in_frames);
    }

    data.update_hash(hasher, offset_in_frames - window_size_, offset_in_frames);
    ++offset_in_frames;
  }

  update_progress(size_in_frames);

  add_data(chkable, frames_written, size_in_frames - frames_written);
  finish_chunk(chkable);
}

template <template <typename> typename SegmentingPolicy, size_t N>
struct constant_granularity_segmenter_ {
  template <typename LoggerPolicy>
  using type =
      segmenter_<LoggerPolicy, SegmentingPolicy<ConstantGranularityPolicy<N>>>;
};

template <template <typename> typename SegmentingPolicy>
struct variable_granularity_segmenter_ {
  template <typename LoggerPolicy>
  using type =
      segmenter_<LoggerPolicy, SegmentingPolicy<VariableGranularityPolicy>>;
};

template <template <typename> typename SegmentingPolicy>
std::unique_ptr<segmenter::impl>
create_segmenter2(logger& lgr, progress& prog,
                  std::shared_ptr<block_manager> blkmgr,
                  segmenter::config const& cfg,
                  compression_constraints const& cc, size_t total_size,
                  segmenter::block_ready_cb block_ready) {
  uint32_t granularity = cc.granularity ? cc.granularity.value() : 1;

  auto make_const_granularity_segmenter = [&]<uint32_t Granularity>() {
    return make_unique_logging_object<
        segmenter::impl,
        constant_granularity_segmenter_<SegmentingPolicy,
                                        Granularity>::template type,
        logger_policies>(lgr, prog, std::move(blkmgr), cfg, total_size,
                         std::move(block_ready));
  };

  switch (granularity) {
  case 1:
    return make_const_granularity_segmenter.template operator()<1>();
  case 2: // 16-bit mono PCM audio
    return make_const_granularity_segmenter.template operator()<2>();
  case 3: // 24-bit mono PCM audio
    return make_const_granularity_segmenter.template operator()<3>();
  case 4: // 16-bit stereo PCM audio
    return make_const_granularity_segmenter.template operator()<4>();
  case 6: // 24-bit stereo PCM audio
    return make_const_granularity_segmenter.template operator()<6>();
  default:
    break;
  }

  return make_unique_logging_object<
      segmenter::impl,
      variable_granularity_segmenter_<SegmentingPolicy>::template type,
      logger_policies>(lgr, prog, std::move(blkmgr), cfg, total_size,
                       std::move(block_ready), cc.granularity.value());
}

std::unique_ptr<segmenter::impl>
create_segmenter(logger& lgr, progress& prog,
                 std::shared_ptr<block_manager> blkmgr,
                 segmenter::config const& cfg,
                 compression_constraints const& cc, size_t total_size,
                 segmenter::block_ready_cb block_ready) {
  if (cfg.max_active_blocks == 0 or cfg.blockhash_window_size == 0) {
    return create_segmenter2<SegmentationDisabledPolicy>(
        lgr, prog, std::move(blkmgr), cfg, cc, total_size,
        std::move(block_ready));
  }

  if (cfg.max_active_blocks == 1) {
    return create_segmenter2<SingleBlockSegmentationPolicy>(
        lgr, prog, std::move(blkmgr), cfg, cc, total_size,
        std::move(block_ready));
  }

  return create_segmenter2<MultiBlockSegmentationPolicy>(
      lgr, prog, std::move(blkmgr), cfg, cc, total_size,
      std::move(block_ready));
}

} // namespace

} // namespace internal

segmenter::segmenter(logger& lgr, writer_progress& prog,
                     std::shared_ptr<internal::block_manager> blkmgr,
                     config const& cfg, compression_constraints const& cc,
                     size_t total_size, block_ready_cb block_ready)
    : impl_(internal::create_segmenter(lgr, prog.get_internal(),
                                       std::move(blkmgr), cfg, cc, total_size,
                                       std::move(block_ready))) {}

size_t segmenter::estimate_memory_usage(config const& cfg,
                                        compression_constraints const& cc) {
  if (cfg.max_active_blocks == 0 or cfg.blockhash_window_size == 0) {
    return 0;
  }

  static constexpr size_t kWorstCaseBytesPerOffset = 19; // 8 bytes / 0.4375

  size_t const granularity = cc.granularity.value_or(1);
  size_t const block_size_in_frames =
      (static_cast<size_t>(1) << cfg.block_size_bits) / granularity;

  size_t const win_size = internal::window_size(cfg);
  size_t const win_step = internal::window_step(cfg);
  size_t const max_offset_count =
      (block_size_in_frames - (win_size - win_step)) / win_step;
  size_t const bloom_filter_mem =
      ((static_cast<size_t>(1) << cfg.bloom_filter_size) *
       std::bit_ceil(cfg.max_active_blocks *
                     (block_size_in_frames / win_step))) /
      8;

  // Single active block uses memory for:
  // - offsets
  // - bloom filter (only with MultiBlockSegmentationPolicy)
  // We do *not* consider the memory for the block data buffer here
  size_t const active_block_mem_usage =
      (max_offset_count * kWorstCaseBytesPerOffset) +
      (cfg.max_active_blocks > 1 ? bloom_filter_mem : 0);

  return cfg.max_active_blocks * active_block_mem_usage + bloom_filter_mem;
}

} // namespace dwarfs::writer
