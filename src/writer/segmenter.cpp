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

  uint64_t total_hashes{0};
  uint64_t l2_collisions{0};
  uint64_t total_matches{0};
  uint64_t good_matches{0};
  uint64_t bad_matches{0};
  uint64_t bloom_lookups{0};
  uint64_t bloom_hits{0};
  uint64_t bloom_true_positives{0};
  folly::Histogram<uint64_t> l2_collision_vec_size;
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

  uint64_t memory_usage() const {
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

  uint64_t memory_usage() const { return size_ / 8; }

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

  static DWARFS_FORCE_INLINE uint64_t bytes_to_frames(uint64_t size) {
    assert(size % kGranularity == 0);
    return size / kGranularity;
  }

  static DWARFS_FORCE_INLINE uint64_t frames_to_bytes(uint64_t size) {
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

  DWARFS_FORCE_INLINE uint64_t bytes_to_frames(uint64_t size) const {
    assert(size % granularity_ == 0);
    return size / granularity_;
  }

  DWARFS_FORCE_INLINE uint64_t frames_to_bytes(uint64_t size) const {
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

class segment_queue {
 public:
  class queue_data {
   public:
    explicit queue_data(file_segments_iterable segments)
        : segments_{std::move(segments)}
        , seg_it_{segments_.begin()} {}

    struct queued_segment {
      explicit queued_segment(file_segment seg)
          : range{seg.range()}
          , bytes{seg.span()}
          , segment{std::move(seg)} {}

      file_range range;
      std::span<std::byte const> bytes;
      file_segment segment;
    };

    struct versioned_iterator {
      versioned_iterator(std::deque<queued_segment>::iterator const& it_,
                         size_t ver)
          : it{it_}
          , version{ver} {}

      std::deque<queued_segment>::iterator it;
      size_t version{0};
    };

    using lookup_hint = std::optional<versioned_iterator>;

    file_range range() const { return segments_.range(); }

    std::span<std::byte const> span(file_off_t offset) {
      auto it = find_segment(offset);
      return it->bytes.subspan(offset - it->range.offset());
    }

    DWARFS_FORCE_INLINE std::byte const&
    at(file_off_t offset, lookup_hint* phint = nullptr) {
      if (phint && phint->has_value()) [[likely]] {
        auto const& hint = **phint;
        if (hint.version == queue_version_) [[likely]] {
          auto const& r = hint.it->range;
          if (offset >= r.offset() && offset < r.end()) [[likely]] {
            return hint.it->bytes[offset - r.offset()];
          }
        }
        phint->reset();
      }

      auto it = find_segment(offset);

      if (phint) [[likely]] {
        phint->emplace(it, queue_version_);
      }

      return it->bytes[offset - it->range.offset()];
    }

    void release_until(file_off_t offset) {
      while (!queue_.empty() && queue_.front().range.end() <= offset) {
        queue_.pop_front();
      }
    }

   private:
    std::deque<queued_segment>::iterator find_segment(file_off_t offset) {
      while (queue_.empty() || offset >= queue_.back().range.end()) {
        if (seg_it_ == segments_.end()) {
          throw std::out_of_range("offset out of range");
        }
        queue_.emplace_back(*seg_it_++);
        ++queue_version_;
      }

      for (auto it = queue_.rbegin(); it != queue_.rend(); ++it) {
        assert(offset < it->range.end());
        if (offset >= it->range.offset()) {
          return std::prev(it.base());
        }
      }

      throw std::out_of_range("offset out of range");
    }

    std::deque<queued_segment> queue_;
    file_segments_iterable segments_;
    file_segments_iterable::iterator seg_it_;
    size_t queue_version_{0};
  };

  class byte_range_iterable {
   public:
    class iterator {
     public:
      using value_type = std::byte;
      using difference_type = std::ptrdiff_t;
      using iterator_category = std::input_iterator_tag;
      using reference = value_type const&;
      using pointer = value_type const*;

      iterator() = default;
      iterator(queue_data* d, file_off_t offset)
          : data_{d}
          , offset_{offset} {}

      DWARFS_FORCE_INLINE reference operator*() {
        return data_->at(offset_, &hint_);
      }
      DWARFS_FORCE_INLINE pointer operator->() { return &**this; }

      DWARFS_FORCE_INLINE iterator& operator++() {
        ++offset_;
        return *this;
      }

      DWARFS_FORCE_INLINE iterator operator++(int) {
        auto tmp = *this;
        ++*this;
        return tmp;
      }

      [[maybe_unused]] friend DWARFS_FORCE_INLINE bool
      operator==(iterator const& a, iterator const& b) noexcept {
        return a.data_ == b.data_ && a.offset_ == b.offset_;
      }

      [[maybe_unused]] friend DWARFS_FORCE_INLINE bool
      operator!=(iterator const& a, iterator const& b) noexcept {
        return !(a == b);
      }

     private:
      queue_data* data_{nullptr};
      file_off_t offset_{0};
      queue_data::lookup_hint hint_;
    };

    iterator begin() { return iterator{data_, range_.begin()}; }

    iterator end() { return iterator{data_, range_.end()}; }

    byte_range_iterable() = default;

    byte_range_iterable(queue_data* d, file_range range)
        : data_{d}
        , range_{range} {}

   private:
    queue_data* data_{nullptr};
    file_range range_;
  };

  class span_range_iterable {
   public:
    class iterator {
     public:
      using value_type = std::span<std::byte const>;
      using difference_type = std::ptrdiff_t;
      using iterator_category = std::input_iterator_tag;
      using reference = value_type const&;
      using pointer = value_type const*;

      iterator() = default;
      iterator(queue_data* d, file_range range)
          : data_{d}
          , cur_{range.begin()}
          , end_{range.end()} {
        advance(true);
      }

      reference operator*() { return span_; }
      pointer operator->() { return &**this; }

      iterator& operator++() {
        advance();
        return *this;
      }

      iterator operator++(int) {
        auto tmp = *this;
        ++*this;
        return tmp;
      }

      [[maybe_unused]] friend bool
      operator==(iterator const& a, std::default_sentinel_t) noexcept {
        return a.cur_ == a.end_;
      }

      [[maybe_unused]] friend bool
      operator!=(iterator const& a, std::default_sentinel_t s) noexcept {
        return !(a == s);
      }

     private:
      void advance(bool initialize = false) {
        if (!initialize) {
          cur_ += span_.size();
        }

        if (cur_ < end_) {
          auto const remain = end_ - cur_;

          span_ = data_->span(cur_);

          if (std::cmp_greater(span_.size(), remain)) {
            span_ = span_.subspan(0, remain);
          }
        } else {
          assert(cur_ == end_);
          span_ = {};
        }
      }

      queue_data* data_{nullptr};
      std::span<std::byte const> span_;
      file_off_t cur_{0};
      file_off_t end_{0};
    };

    span_range_iterable(queue_data* d, file_range range)
        : data_{d}
        , range_{range} {}

    iterator begin() { return iterator{data_, range_}; }

    std::default_sentinel_t end() { return {}; }

   private:
    queue_data* data_{nullptr};
    file_range range_;
  };

  explicit segment_queue(file_segments_iterable segments)
      : data_{std::make_unique<queue_data>(std::move(segments))} {}

  byte_range_iterable byte_range(file_range range) {
    return byte_range_iterable{data_.get(), range};
  }

  byte_range_iterable byte_range() {
    return byte_range_iterable{data_.get(), data_->range()};
  }

  span_range_iterable span_range(file_range range) {
    return span_range_iterable{data_.get(), range};
  }

  span_range_iterable span_range() {
    return span_range_iterable{data_.get(), data_->range()};
  }

  void release_until(file_off_t offset) { data_->release_until(offset); }

 private:
  std::unique_ptr<queue_data> data_;
};

template <typename GranularityPolicy, bool UseSegmentQueue>
class granular_extent_adapter;

template <typename GranularityPolicy>
class granular_extent_adapter<GranularityPolicy, false>
    : private GranularityPolicy {
 public:
  static constexpr bool kUseSegmentQueue{false};

  using value_type = uint8_t;

  template <typename... PolicyArgs>
  DWARFS_FORCE_INLINE
  granular_extent_adapter(file_extent const& extent, PolicyArgs&&... args)
      : GranularityPolicy(std::forward<PolicyArgs>(args)...)
      , raw_bytes_{get_raw_bytes(extent)}
      , ext_{extent} {}

  DWARFS_FORCE_INLINE file_size_t size() const {
    return this->bytes_to_frames(raw_bytes_.size());
  }

  DWARFS_FORCE_INLINE file_size_t size_in_bytes() const {
    return raw_bytes_.size();
  }

  template <typename H>
  DWARFS_FORCE_INLINE void update_hash(H& hasher, file_off_t offset) const {
    offset = this->frames_to_bytes(offset);
    this->for_bytes_in_frame([&] { hasher.update(raw_bytes_[offset++]); });
  }

  template <typename H>
  DWARFS_FORCE_INLINE void
  update_hash(H& hasher, file_off_t from, file_off_t to) const {
    from = this->frames_to_bytes(from);
    to = this->frames_to_bytes(to);
    this->for_bytes_in_frame(
        [&] { hasher.update(raw_bytes_[from++], raw_bytes_[to++]); });
  }

  DWARFS_FORCE_INLINE void
  append_to(auto& v, file_off_t offset, file_size_t size) const {
    v.append(raw_bytes_.data() + offset, size);
  }

  DWARFS_FORCE_INLINE int
  compare(file_off_t offset, std::span<value_type const> rhs) const {
    auto const offset_in_bytes = this->frames_to_bytes(offset);
    assert(std::cmp_less_equal(offset_in_bytes + rhs.size(), ext_.size()));
    return std::memcmp(raw_bytes_.data() + offset_in_bytes, rhs.data(),
                       rhs.size());
  }

  DWARFS_FORCE_INLINE size_t
  match_backward(file_off_t offset, std::span<value_type const> rhs) const {
    auto const offset_in_bytes = this->frames_to_bytes(offset);
    assert(std::cmp_less_equal(offset_in_bytes + rhs.size(), ext_.size()));

    std::span<value_type const> lhs{raw_bytes_.data() + offset_in_bytes,
                                    rhs.size()};
    // NOLINTBEGIN(modernize-use-ranges)
    auto [lit, rit] =
        std::mismatch(lhs.rbegin(), lhs.rend(), rhs.rbegin(), rhs.rend());
    // NOLINTEND(modernize-use-ranges)

    size_t match_length = std::distance(lhs.rbegin(), lit);
    match_length -= match_length % this->granularity_bytes();

    return this->bytes_to_frames(match_length);
  }

  DWARFS_FORCE_INLINE size_t
  match_forward(file_off_t offset, std::span<value_type const> rhs) const {
    auto const offset_in_bytes = this->frames_to_bytes(offset);
    assert(std::cmp_less_equal(offset_in_bytes + rhs.size(), ext_.size()));

    std::span<value_type const> lhs{raw_bytes_.data() + offset_in_bytes,
                                    rhs.size()};
    // NOLINTBEGIN(modernize-use-ranges)
    auto [lit, rit] =
        std::mismatch(lhs.begin(), lhs.end(), rhs.begin(), rhs.end());
    // NOLINTEND(modernize-use-ranges)

    size_t match_length = std::distance(lhs.begin(), lit);
    match_length -= match_length % this->granularity_bytes();

    return this->bytes_to_frames(match_length);
  }

  void release_until(file_off_t offset) {
    // TODO
    static_cast<void>(
        ext_.release_until(ext_.offset() + this->frames_to_bytes(offset)));
  }

 private:
  static std::span<value_type const> get_raw_bytes(file_extent const& ext) {
    assert(ext.supports_raw_bytes());
    auto const span = ext.raw_bytes();
    return {reinterpret_cast<value_type const*>(span.data()), span.size()};
  }

  std::span<value_type const> raw_bytes_;
  file_extent const& ext_;
};

template <typename GranularityPolicy>
class granular_extent_adapter<GranularityPolicy, true>
    : private GranularityPolicy {
 public:
  static constexpr bool kUseSegmentQueue{true};

  using value_type = uint8_t;

  template <typename... PolicyArgs>
  DWARFS_FORCE_INLINE
  granular_extent_adapter(file_extent const& extent, PolicyArgs&&... args)
      : GranularityPolicy(std::forward<PolicyArgs>(args)...)
      , ext_{extent}
      , queue_{ext_.segments()} {}

  DWARFS_FORCE_INLINE file_size_t size() const {
    return this->bytes_to_frames(ext_.size());
  }

  DWARFS_FORCE_INLINE file_size_t size_in_bytes() const { return ext_.size(); }

  segment_queue::byte_range_iterable byte_range(file_off_t offset) const {
    offset = this->frames_to_bytes(offset);
    return queue_.byte_range({ext_.offset() + offset, ext_.size() - offset});
  }

  template <typename H, typename It>
  DWARFS_FORCE_INLINE void update_hash(H& hasher, It& it) const {
    this->for_bytes_in_frame([&] {
      hasher.update(static_cast<uint8_t>(*it));
      ++it;
    });
  }

  template <typename H, typename It>
  DWARFS_FORCE_INLINE void update_hash(H& hasher, It& from, It& to) const {
    this->for_bytes_in_frame([&] {
      hasher.update(static_cast<uint8_t>(*from), static_cast<uint8_t>(*to));
      ++from;
      ++to;
    });
  }

  DWARFS_FORCE_INLINE void
  append_to(auto& v, file_off_t offset, file_size_t size) const {
    for (auto const& span : this->get_span_range(offset, size)) {
      v.append(span.data(), span.size());
    }
  }

  DWARFS_FORCE_INLINE int
  compare(file_off_t offset, std::span<value_type const> rhs) const {
    auto const offset_in_bytes = this->frames_to_bytes(offset);

    for (auto const& span : this->get_span_range(offset_in_bytes, rhs.size())) {
      auto const cmp = std::memcmp(span.data(), rhs.data(), span.size());
      if (cmp != 0) {
        return cmp;
      }
      rhs = rhs.subspan(span.size());
    }

    return 0;
  }

  DWARFS_FORCE_INLINE size_t
  match_backward(file_off_t offset, std::span<value_type const> rhs) const {
    auto const offset_in_bytes = this->frames_to_bytes(offset);
    auto const spans = collect_spans(offset_in_bytes, rhs.size());
    size_t match_length{0};

    for (auto it = spans.rbegin(); it != spans.rend(); ++it) {
      auto const& lhs = *it;
      auto const [lit, rit] =
          std::mismatch(lhs.rbegin(), lhs.rend(), rhs.rbegin(), rhs.rend());
      auto const submatch_length = std::distance(lhs.rbegin(), lit);
      match_length += submatch_length;
      if (lit != lhs.rend() && rit != rhs.rend()) {
        break;
      }
      rhs = rhs.subspan(0, rhs.size() - submatch_length);
    }

    match_length -= match_length % this->granularity_bytes();

    return this->bytes_to_frames(match_length);
  }

  DWARFS_FORCE_INLINE size_t
  match_forward(file_off_t offset, std::span<value_type const> rhs) const {
    auto const offset_in_bytes = this->frames_to_bytes(offset);
    auto const spans = collect_spans(offset_in_bytes, rhs.size());
    size_t match_length{0};

    for (auto const& lhs : spans) {
      auto const [lit, rit] =
          std::mismatch(lhs.begin(), lhs.end(), rhs.begin(), rhs.end());
      auto const submatch_length = std::distance(lhs.begin(), lit);
      match_length += submatch_length;
      if (lit != lhs.end() && rit != rhs.end()) {
        break;
      }
      rhs = rhs.subspan(submatch_length);
    }

    match_length -= match_length % this->granularity_bytes();

    return this->bytes_to_frames(match_length);
  }

  void release_until(file_off_t offset) {
    queue_.release_until(ext_.offset() + offset);
  }

 private:
  DWARFS_FORCE_INLINE auto
  collect_spans(file_off_t offset_in_bytes, file_size_t size_in_bytes) const {
    folly::small_vector<std::span<value_type const>, 8> spans;

    for (auto const& span : get_span_range(offset_in_bytes, size_in_bytes)) {
      spans.emplace_back(reinterpret_cast<value_type const*>(span.data()),
                         span.size());
    }

    return spans;
  }

  DWARFS_FORCE_INLINE auto
  get_span_range(file_off_t offset_in_bytes, file_size_t size_in_bytes) const {
    assert(offset_in_bytes + size_in_bytes <= ext_.size());

    return queue_.span_range({ext_.offset() + offset_in_bytes, size_in_bytes});
  }

  file_extent const& ext_;
  segment_queue mutable queue_;
};

template <bool UseSegmentQueue>
class hash_window;

template <>
class hash_window<false> {
 public:
  explicit hash_window(size_t window_size) noexcept
      : window_size_{window_size} {}

  template <typename Hasher, typename ExtentAdapter>
  DWARFS_FORCE_INLINE file_off_t seek(Hasher& hasher, ExtentAdapter const& data,
                                      file_off_t offset) {
    file_off_t end = offset + window_size_;
    for (; offset < end; ++offset) {
      data.update_hash(hasher, offset);
    }
    return offset;
  }

  template <typename Hasher, typename ExtentAdapter>
  DWARFS_FORCE_INLINE file_off_t slide(Hasher& hasher,
                                       ExtentAdapter const& data,
                                       file_off_t offset) {
    data.update_hash(hasher, offset - window_size_, offset);
    return offset + 1;
  }

 private:
  size_t const window_size_{0};
};

template <>
class hash_window<true> {
 public:
  explicit hash_window(size_t window_size) noexcept
      : window_size_{window_size} {}

  template <typename Hasher, typename ExtentAdapter>
  DWARFS_FORCE_INLINE file_off_t seek(Hasher& hasher, ExtentAdapter const& data,
                                      file_off_t offset) {
    from_range_ = data.byte_range(offset);
    from_it_ = from_range_.begin();
    to_range_ = data.byte_range(offset);
    to_it_ = to_range_.begin();

    file_off_t end = offset + window_size_;

    for (; offset < end; ++offset) {
      data.update_hash(hasher, to_it_);
    }

    return offset;
  }

  template <typename Hasher, typename ExtentAdapter>
  DWARFS_FORCE_INLINE file_off_t slide(Hasher& hasher,
                                       ExtentAdapter const& data,
                                       file_off_t offset) {
    data.update_hash(hasher, from_it_, to_it_);
    return offset + 1;
  }

 private:
  size_t const window_size_{0};
  segment_queue::byte_range_iterable from_range_;
  segment_queue::byte_range_iterable to_range_;
  segment_queue::byte_range_iterable::iterator from_it_;
  segment_queue::byte_range_iterable::iterator to_it_;
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

  template <bool UseSegmentQueue>
  DWARFS_FORCE_INLINE void
  append(granular_extent_adapter<GranularityPolicy, UseSegmentQueue> const& ext,
         file_off_t offset, file_size_t size) {
    ext.append_to(v_, offset, size);
  }

  DWARFS_FORCE_INLINE std::span<value_type const>
  subspan(file_off_t offset, file_size_t size) const {
    auto const offset_in_bytes = this->frames_to_bytes(offset);
    auto const size_in_bytes = this->frames_to_bytes(size);
    assert(offset_in_bytes + size_in_bytes <= v_.size());
    return std::span<value_type const>(v_.data() + offset_in_bytes,
                                       size_in_bytes);
  }

  template <typename H>
  DWARFS_FORCE_INLINE void update_hash(H& hasher, file_off_t offset) const {
    auto p = v_.data() + this->frames_to_bytes(offset);
    this->for_bytes_in_frame([&] { hasher.update(*p++); });
  }

  template <typename H>
  DWARFS_FORCE_INLINE void
  update_hash(H& hasher, file_off_t from, file_off_t to) const {
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

  template <bool UseSegmentQueue>
  DWARFS_FORCE_INLINE void append_bytes(
      granular_extent_adapter<GranularityPolicy, UseSegmentQueue>& data,
      file_off_t offset, file_size_t size, bloom_filter& global_filter);

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

  segmenter_progress(std::string context, uint64_t total_size)
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
  std::atomic<uint64_t> bytes_processed{0};
  // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes)

 private:
  std::string const context_;
  uint64_t const bytes_total_;
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
  template <bool UseSegmentQueue>
  using extent_adapter_t =
      granular_extent_adapter<GranularityPolicyT, UseSegmentQueue>;

 public:
  template <typename... PolicyArgs>
  segmenter_(logger& lgr, progress& prog, std::shared_ptr<block_manager> blkmgr,
             segmenter::config const& cfg, uint64_t total_size,
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
    file_off_t offset_in_frames{0};
    file_size_t size_in_frames{0};
  };

  DWARFS_FORCE_INLINE void block_ready();
  void finish_chunk(chunkable& chkable);
  template <bool UseSegmentQueue>
  DWARFS_FORCE_INLINE void
  append_to_block(chunkable& chkable, extent_adapter_t<UseSegmentQueue>& data,
                  file_off_t offset_in_frames, file_size_t size_in_frames);
  template <bool UseSegmentQueue>
  void add_data(chunkable& chkable, extent_adapter_t<UseSegmentQueue>& data,
                file_off_t offset_in_frames, file_size_t size_in_frames);
  template <bool UseSegmentQueue>
  DWARFS_FORCE_INLINE void
  segment_and_add_data(chunkable& chkable,
                       extent_adapter_t<UseSegmentQueue>& data,
                       file_size_t size_in_frames);

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

  folly::Histogram<uint64_t> match_counts_;
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

  template <bool UseSegmentQueue>
  void verify_and_extend(
      granular_extent_adapter<GranularityPolicy, UseSegmentQueue> const& data,
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
template <bool UseSegmentQueue>
DWARFS_FORCE_INLINE void
active_block<LoggerPolicy, GranularityPolicy>::append_bytes(
    granular_extent_adapter<GranularityPolicy, UseSegmentQueue>& data,
    file_off_t data_offset, file_size_t data_size,
    bloom_filter& global_filter) {
  auto v = this->template create<granular_buffer_adapter<GranularityPolicy>>(
      data_.raw_buffer());

  // TODO: this works in theory, but slows down the segmenter by almost 10%
  // auto v = this->template create<
  //     granular_byte_buffer_adapter<GranularityPolicy>>(data_);

  auto offset = v.size();

  DWARFS_CHECK(offset + bytes_to_frames(data_size) <= capacity_in_frames_,
               fmt::format("block capacity exceeded: {} + {} > {}", offset,
                           data_size, frames_to_bytes(capacity_in_frames_)));

  v.append(data, data_offset, data_size);

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
template <bool UseSegmentQueue>
void segment_match<LoggerPolicy, GranularityPolicy>::verify_and_extend(
    granular_extent_adapter<GranularityPolicy, UseSegmentQueue> const& data,
    size_t pos, size_t len, size_t begin, size_t end) {
  auto v = this->template create<granular_buffer_adapter<GranularityPolicy>>(
      block_->data().raw_buffer());

  // First, check if the regions actually match
  if (data.compare(pos, v.subspan(offset_, len)) == 0) {
    if (pos > begin + offset_) {
      begin = pos - offset_;
    }

    if (end - pos > v.size() - offset_) {
      end = pos + (v.size() - offset_);
    }

    auto const prefixlen = pos - begin;
    auto const prefixmatchlen =
        data.match_backward(begin, v.subspan(offset_ - prefixlen, prefixlen));

    auto const suffixlen = end - (pos + len);
    auto const suffixmatchlen =
        data.match_forward(pos + len, v.subspan(offset_ + len, suffixlen));

    offset_ -= prefixmatchlen;
    pos_ = pos - prefixmatchlen;
    size_ = len + prefixmatchlen + suffixmatchlen;
  }

  // No match, this was a hash collision, we're done.
  // size_ defaults to 0 unless we have a real match and set it above.
}

template <typename LoggerPolicy, typename SegmentingPolicy>
void segmenter_<LoggerPolicy, SegmentingPolicy>::add_chunkable(
    chunkable& chkable) {
  if (chkable.size() > 0) {
    LOG_TRACE << cfg_.context << "adding " << chkable.description();

    pctx_->current_file = chkable.get_file();

    auto process_extent = [&](auto& data) {
      auto const size_in_frames = data.size();

      if (!is_segmentation_enabled() or
          std::cmp_less(size_in_frames, window_size_)) {
        // no point dealing with hashing, just write it out
        add_data(chkable, data, 0, size_in_frames);
        finish_chunk(chkable);
        auto const size_in_bytes = data.size_in_bytes();
        prog_.total_bytes_read += size_in_bytes;
        pctx_->bytes_processed += size_in_bytes;
      } else {
        segment_and_add_data(chkable, data, size_in_frames);
      }
    };

    for (auto const& ext : chkable.extents()) {
      if (cfg_.enable_sparse_files && ext.kind() == extent_kind::hole) {
        chkable.add_hole(ext.size());
      } else if (ext.supports_raw_bytes()) {
        auto data = this->template create<extent_adapter_t<false>>(ext);
        process_extent(data);
      } else {
        auto data = this->template create<extent_adapter_t<true>>(ext);
        process_extent(data);
      }
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
template <bool UseSegmentQueue>
DWARFS_FORCE_INLINE void
segmenter_<LoggerPolicy, SegmentingPolicy>::append_to_block(
    chunkable& chkable, extent_adapter_t<UseSegmentQueue>& data,
    file_off_t offset_in_frames, file_size_t size_in_frames) {
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

  block.append_bytes(data, offset_in_bytes, size_in_bytes, global_filter_);
  chunk_.size_in_frames += size_in_frames;

  prog_.filesystem_size += size_in_bytes;

  if (block.full()) [[unlikely]] {
    data.release_until(offset_in_bytes + size_in_bytes);
    finish_chunk(chkable);
    block_ready();
  }
}

template <typename LoggerPolicy, typename SegmentingPolicy>
template <bool UseSegmentQueue>
void segmenter_<LoggerPolicy, SegmentingPolicy>::add_data(
    chunkable& chkable, extent_adapter_t<UseSegmentQueue>& data,
    file_off_t offset_in_frames, file_size_t size_in_frames) {
  while (size_in_frames > 0) {
    file_off_t block_offset_in_frames = 0;

    if (!blocks_.empty()) {
      block_offset_in_frames = blocks_.back().size_in_frames();
    }

    auto const chunk_size_in_frames = std::min<file_size_t>(
        size_in_frames, block_size_in_frames_ - block_offset_in_frames);

    append_to_block(chkable, data, offset_in_frames, chunk_size_in_frames);

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
template <bool UseSegmentQueue>
DWARFS_FORCE_INLINE void
segmenter_<LoggerPolicy, SegmentingPolicy>::segment_and_add_data(
    chunkable& chkable, extent_adapter_t<UseSegmentQueue>& data,
    file_size_t size_in_frames) {
  rsync_hash hasher;
  file_off_t offset_in_frames = 0;
  file_size_t frames_written = 0;
  size_t lookback_size_in_frames = window_size_ + window_step_;
  size_t next_hash_offset_in_frames =
      lookback_size_in_frames +
      (blocks_.empty() ? window_step_
                       : blocks_.back().next_hash_distance_in_frames());

  DWARFS_CHECK(std::cmp_greater_equal(size_in_frames, window_size_),
               "unexpected call to segment_and_add_data");

  hash_window<UseSegmentQueue> hashwin(window_size_);

  offset_in_frames = hashwin.seek(hasher, data, 0);

  folly::small_vector<segment_match<LoggerPolicy, GranularityPolicyT>, 1>
      matches;

  // // TODO: we have multiple segmenter threads, so this doesn't fly anymore
  // auto total_bytes_read_before = prog_.total_bytes_read.load();
  // prog_.current_offset.store(
  //     frames_to_bytes(offset_in_frames)); // TODO: what do we do with this?
  // prog_.current_size.store(frames_to_bytes(size_in_frames)); // TODO

  // TODO: how can we reasonably update the top progress bar with
  //       multiple concurrent segmenters?

  auto update_progress = [this, last_offset = static_cast<file_off_t>(0)](
                             file_off_t offset) mutable {
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
          add_data(chkable, data, frames_written, num_to_write);
          frames_written += num_to_write;
          finish_chunk(chkable);

          chkable.add_chunk(block_num, frames_to_bytes(match_off),
                            frames_to_bytes(match_len));

          prog_.chunk_count++;
          frames_written += match_len;

          prog_.saved_by_segmentation += frames_to_bytes(match_len);

          offset_in_frames = frames_written;

          if (std::cmp_less(size_in_frames - frames_written, window_size_)) {
            break;
          }

          hasher.clear();

          offset_in_frames = hashwin.seek(hasher, data, offset_in_frames);

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

    if (std::cmp_equal(offset_in_frames, next_hash_offset_in_frames))
        [[unlikely]] {
      auto num_to_write =
          offset_in_frames - lookback_size_in_frames - frames_written;
      add_data(chkable, data, frames_written, num_to_write);
      frames_written += num_to_write;
      next_hash_offset_in_frames += window_step_;

      update_progress(offset_in_frames);
    }

    offset_in_frames = hashwin.slide(hasher, data, offset_in_frames);
  }

  update_progress(size_in_frames);

  add_data(chkable, data, frames_written, size_in_frames - frames_written);
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
                  compression_constraints const& cc, file_size_t total_size,
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
                 compression_constraints const& cc, file_size_t total_size,
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
                     file_size_t total_size, block_ready_cb block_ready)
    : impl_(internal::create_segmenter(lgr, prog.get_internal(),
                                       std::move(blkmgr), cfg, cc, total_size,
                                       std::move(block_ready))) {}

uint64_t segmenter::estimate_memory_usage(config const& cfg,
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
