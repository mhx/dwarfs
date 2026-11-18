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
#include <atomic>
#include <bit>
#include <cassert>
#include <chrono>
#include <limits>
#include <memory>
#include <numeric>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(__aarch64__)
#include <arm_neon.h>
#endif

#if defined(__linux__) && (defined(__aarch64__) || defined(__arm__))
#include <asm/hwcap.h>
#include <sys/auxv.h>
#endif

#include <fmt/format.h>

#include <dwarfs/compiler.h>
#include <dwarfs/container/packed_int_vector.h>
#include <dwarfs/logger.h>

#include <dwarfs/internal/worker_group.h>
#include <dwarfs/writer/internal/progress.h>
#include <dwarfs/writer/internal/similarity_ordering.h>
#include <dwarfs/writer/internal/sort_index.h>

namespace dwarfs::writer::internal {

using namespace dwarfs::internal;

namespace {

using hash_type = std::array<uint64_t, 4>;

DWARFS_FORCE_INLINE int
distance_portable(hash_type const& a, hash_type const& b) {
  int d = 0;
  for (size_t i = 0; i < a.size(); ++i) {
    d += std::popcount(a[i] ^ b[i]);
  }
  return d;
}

#if defined(__aarch64__)
DWARFS_FORCE_INLINE int distance_neon(hash_type const& a, hash_type const& b) {
  auto const x0 = veorq_u64(vld1q_u64(a.data()), vld1q_u64(b.data()));
  auto const x1 = veorq_u64(vld1q_u64(a.data() + 2), vld1q_u64(b.data() + 2));
  auto const c0 = vcntq_u8(vreinterpretq_u8_u64(x0));
  auto const c1 = vcntq_u8(vreinterpretq_u8_u64(x1));
  return vaddlvq_u8(vaddq_u8(c0, c1));
}
#elif defined(__linux__) && defined(__arm__)
DWARFS_FORCE_INLINE int distance_neon(hash_type const& a, hash_type const& b) {
  int d;
  __asm__(".fpu neon\n\t"
          "vld1.8 {d0-d3}, [%[a]]\n\t"
          "vld1.8 {d4-d7}, [%[b]]\n\t"
          "veor q0, q0, q2\n\t"
          "veor q1, q1, q3\n\t"
          "vcnt.8 q0, q0\n\t"
          "vcnt.8 q1, q1\n\t"
          "vadd.i8 q0, q0, q1\n\t"
          "vpaddl.u8 q0, q0\n\t"
          "vpaddl.u16 q0, q0\n\t"
          "vpaddl.u32 q0, q0\n\t"
          "vadd.i64 d0, d0, d1\n\t"
          "vmov.32 %[d], d0[0]"
          : [d] "=r"(d)
          : [a] "r"(a.data()), [b] "r"(b.data()), "m"(a), "m"(b)
          : "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7");
  return d;
}
#endif

using rank_type = uint32_t;
constexpr auto invalid_rank = std::numeric_limits<rank_type>::max();
constexpr size_t exact_tail = 256;

// Fixed SplitMix64 stream and Fisher-Yates shuffle
inline uint64_t next_random(uint64_t& state) {
  auto z = (state += 0x9e3779b97f4a7c15ULL);
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
  return z ^ (z >> 31);
}

// Project onto the first 64 bits of a random bit permutation. A byte table
// makes projection construction cheap and independent of native byte order.
class projection {
 public:
  explicit projection(uint64_t& seed) {
    std::array<unsigned, 256> bits;
    std::iota(bits.begin(), bits.end(), 0);

    for (unsigned i = 0; i < 64; ++i) {
      auto j = i + next_random(seed) % (bits.size() - i);
      std::swap(bits[i], bits[j]);
      auto bit = bits[i];
      selected_[i] = static_cast<uint8_t>(bit);

      for (unsigned v = 0; v < 256; ++v) {
        if (v & (1U << (bit % 8))) {
          table_[bit / 8][v] |= uint64_t{1} << (63 - i);
        }
      }
    }
  }

  uint64_t operator()(hash_type const& hash) const {
    uint64_t key = 0;

    for (size_t word = 0; word < hash.size(); ++word) {
      auto h = hash[word];

      for (size_t byte = 0; byte < 8; ++byte) {
        key |= table_[8 * word + byte][h & 0xff];
        h >>= 8;
      }
    }

    return key;
  }

  auto const& selected_bits() const { return selected_; }

 private:
  std::array<uint8_t, 64> selected_{};
  std::array<std::array<uint64_t, 256>, 32> table_{};
};

// The prefix and canonical rank fit in one native integer. Refine equal
// prefixes with the full projection key to preserve the exact sort order.
// Within a run, the prefix is shared, so the remaining key bits and the rank
// (both at most `rank_bits` wide) fit in one integer as well, and each key is
// projected only once.
template <typename HashAt>
void sort_projection(std::vector<uint64_t>& keys, projection const& project,
                     unsigned rank_bits, HashAt const& hash_at) {
  auto const mask = (uint64_t{1} << rank_bits) - 1;

  for (size_t i = 0; i < keys.size(); ++i) {
    keys[i] = (project(hash_at(i)) & ~mask) | i;
  }

  std::ranges::sort(std::span(keys));

  for (size_t first = 0; first < keys.size();) {
    auto const prefix = keys[first] >> rank_bits;
    auto last = first + 1;

    while (last < keys.size() && (keys[last] >> rank_bits) == prefix) {
      ++last;
    }

    if (last - first > 1) {
      auto const run = std::span(keys).subspan(first, last - first);
      auto const prefix_key = run.front() & ~mask;

      for (auto& k : run) {
        auto const rank = k & mask;
        k = ((project(hash_at(rank)) & mask) << rank_bits) | rank;
      }

      std::ranges::sort(run);

      for (auto& k : run) {
        k = prefix_key | (k & mask);
      }
    }

    first = last;
  }
}

// Use a bitmap for each group of 64 sorted positions, and link only the
// nonempty groups. This allow to skip long runs of deleted positions in
// constant time, with only two links per 64 ranks.
class active_positions {
 public:
  explicit active_positions(size_t n)
      : blocks_{n / 64 + (n % 64 != 0)} {
    assert(n != 0);

    for (size_t i = 0; i < blocks_.size(); ++i) {
      blocks_[i] = {~uint64_t{0}, i == 0 ? invalid_rank : rank_type(i - 1),
                    i + 1 == blocks_.size() ? invalid_rank : rank_type(i + 1)};
    }

    if (auto const tail = n % 64; tail != 0) {
      blocks_.back().mask = (uint64_t{1} << tail) - 1;
    }
  }

  size_t capacity_in_bytes() const {
    return blocks_.capacity() * sizeof(block);
  }

  void erase(rank_type pos) {
    auto& b = blocks_[pos / 64];

    b.mask &= ~(uint64_t{1} << (pos % 64));

    if (b.mask == 0) {
      if (b.prev != invalid_rank) {
        blocks_[b.prev].next = b.next;
      }

      if (b.next != invalid_rank) {
        blocks_[b.next].prev = b.prev;
      }

      // Keep the old links, as cursors initialized at this block can still
      // walk out of it until the next erase operation.
    }
  }

  struct cursor {
    rank_type block_index;
    uint64_t mask;
  };

  std::pair<cursor, cursor> around(rank_type pos) const {
    auto const index = pos / 64;
    auto const bit = uint64_t{1} << (pos % 64);
    auto const mask = blocks_[index].mask;
    return {{index, mask & (bit - 1)}, {index, mask & ~(bit | (bit - 1))}};
  }

  template <bool Forward>
  rank_type take(cursor& c, rank_type end) const {
    if (c.mask == 0) {
      if (c.block_index == invalid_rank) {
        return end;
      }

      auto const& b = blocks_[c.block_index];

      c.block_index = Forward ? b.next : b.prev;

      if (c.block_index == invalid_rank) {
        return end;
      }

      c.mask = blocks_[c.block_index].mask;
    }

    unsigned bit;

    if constexpr (Forward) {
      bit = std::countr_zero(c.mask);
      c.mask &= c.mask - 1;
    } else {
      bit = 63 - std::countl_zero(c.mask);
      c.mask ^= uint64_t{1} << bit;
    }

    return c.block_index * 64 + bit;
  }

 private:
  struct block {
    uint64_t mask;
    rank_type prev;
    rank_type next;
  };

  std::vector<block> blocks_;
};

// Immutable projection-sorted ranks. For the inverse mapping, keep only the
// low bits of the position relative to its projection-prefix bucket.
// The offset may be omitted entirely. Search at the corresponding stride,
// using binary search for large/skewed buckets.
class sorted_projection {
 public:
  sorted_projection(std::vector<uint64_t> const& keys,
                    projection const& project, unsigned rank_bits,
                    unsigned prefix_bits, unsigned offset_bits)
      : selected_{project.selected_bits()}
      , prefix_bits_{prefix_bits}
      , ranks_(rank_bits, keys.size())
      , stride_{size_t{1} << offset_bits}
      , local_(offset_bits, offset_bits ? keys.size() : 0)
      , directory_((size_t{1} << prefix_bits) + 1)
      , active_{keys.size()} {
    auto const rank_mask = (uint64_t{1} << rank_bits) - 1;
    size_t pos = 0;

    for (size_t b = 0; b + 1 < directory_.size(); ++b) {
      auto const start = pos;

      directory_[b] = static_cast<rank_type>(start);

      while (pos < keys.size() &&
             (prefix_bits == 0 ? 0 : keys[pos] >> (64 - prefix_bits)) == b) {
        auto const rank = static_cast<rank_type>(keys[pos] & rank_mask);

        ranks_.set(pos, rank);

        if (!local_.empty()) {
          local_.set(rank, static_cast<uint8_t>((pos - start) & (stride_ - 1)));
        }

        ++pos;
      }
    }

    directory_.back() = static_cast<rank_type>(pos);

    assert(pos == keys.size());
  }

  size_t capacity_in_bytes() const {
    return ranks_.capacity_in_bytes() + local_.capacity_in_bytes() +
           directory_.capacity() * sizeof(rank_type) +
           active_.capacity_in_bytes();
  }

  rank_type rank(rank_type pos) const { return ranks_.get(pos); }

  auto around(rank_type pos) const { return active_.around(pos); }

  rank_type prev(active_positions::cursor& c, rank_type end) const {
    return active_.take<false>(c, end);
  }

  rank_type next(active_positions::cursor& c, rank_type end) const {
    return active_.take<true>(c, end);
  }

  void erase(rank_type pos) { active_.erase(pos); }

  template <typename HashAt>
  rank_type position(rank_type rank, HashAt const& hash_at) const {
    auto const& hash = hash_at(rank);
    auto const bucket = project_bits(hash, prefix_bits_);
    auto const first =
        size_t{directory_[bucket]} + (local_.empty() ? 0 : local_.get(rank));
    auto const last = size_t{directory_[bucket + 1]};

    assert(first < last);

    if (ranks_.get(first) == rank) {
      return static_cast<rank_type>(first);
    }

    auto const count = (last - first - 1) / stride_ + 1;

    if (count <= 8) {
      for (size_t i = 1; i < count; ++i) {
        auto const pos = first + stride_ * i;
        if (ranks_.get(pos) == rank) {
          return static_cast<rank_type>(pos);
        }
      }
    }

    size_t lo = 1, hi = count;

    while (lo < hi) {
      auto const mid = lo + (hi - lo) / 2;
      auto const other = ranks_.get(first + stride_ * mid);

      if (other == rank) {
        return static_cast<rank_type>(first + stride_ * mid);
      }

      if (projected_less(hash_at(other), other, hash, rank)) {
        lo = mid + 1;
      } else {
        hi = mid;
      }
    }

    // A rank is present even after it has been marked inactive.
    assert(lo < count && ranks_.get(first + stride_ * lo) == rank);

    return static_cast<rank_type>(first + stride_ * lo);
  }

 private:
  // Compare (key(a), ra) < (key(b), rb) for two hashes in the same bucket.
  // Only the first differing selected bit matters, so avoid building keys.
  bool projected_less(hash_type const& a, rank_type ra, hash_type const& b,
                      rank_type rb) const {
    hash_type diff;

    for (size_t w = 0; w < diff.size(); ++w) {
      diff[w] = a[w] ^ b[w];
    }

    for (unsigned i = prefix_bits_; i < selected_.size(); ++i) {
      auto const bit = selected_[i];

      if ((diff[bit / 64] >> (bit % 64)) & 1) {
        return (b[bit / 64] >> (bit % 64)) & 1;
      }
    }

    return ra < rb;
  }

  uint64_t project_bits(hash_type const& hash, unsigned count) const {
    uint64_t key = 0;
    for (unsigned i = 0; i < count; ++i) {
      auto const bit = selected_[i];
      key = (key << 1) | ((hash[bit / 64] >> (bit % 64)) & 1);
    }
    return key;
  }

  std::array<uint8_t, 64> selected_;
  unsigned prefix_bits_;
  container::packed_int_vector<rank_type> ranks_;
  size_t stride_;
  container::packed_int_vector<uint8_t> local_;
  std::vector<rank_type> directory_;
  active_positions active_;
};

class nilsimsa_preparation_context final : public progress::context {
 public:
  using progress::context::status;

  enum class phase {
    sorting_index,
    finding_duplicates,
    sorting_unique,
    caching
  };

  nilsimsa_preparation_context(std::string_view category, size_t count)
      : label_{fmt::format("{}[nilsimsa] ", category)}
      , count_{count} {}

  status get_status() const override {
    auto const phase = phase_.load(std::memory_order_acquire);
    std::string message;

    switch (phase) {
    case phase::sorting_index:
      message = fmt::format("sorting index of {} elements", count_);
      break;
    case phase::finding_duplicates:
      message = fmt::format("finding duplicates in {} elements", count_);
      break;
    case phase::sorting_unique:
      message = fmt::format("sorting {} unique hashes",
                            unique_count_.load(std::memory_order_relaxed));
      break;
    case phase::caching:
      message = fmt::format("preparing pointers for {} unique hashes",
                            unique_count_.load(std::memory_order_relaxed));
      break;
    }

    return {.color = termcolor::YELLOW,
            .context = label_,
            .status_string = std::move(message)};
  }

  void update(phase next, size_t unique_count = 0) {
    if (next == phase::sorting_unique) {
      unique_count_.store(unique_count, std::memory_order_relaxed);
    }
    phase_.store(next, std::memory_order_release);
  }

 private:
  std::string const label_;
  size_t const count_;
  std::atomic<size_t> unique_count_{0};
  std::atomic<phase> phase_{phase::sorting_index};
};

class nilsimsa_progress_context final : public progress::context {
 public:
  using progress::context::status;

  nilsimsa_progress_context(std::string_view category, std::string phase,
                            std::string unit, size_t total)
      : label_{fmt::format("{}[nilsimsa] ", category)}
      , phase_{std::move(phase)}
      , unit_{std::move(unit)}
      , total_{total} {}

  status get_status() const override {
    auto const completed = completed_.load(std::memory_order_relaxed);
    return {.color = termcolor::YELLOW,
            .context = label_,
            .status_string = fmt::format("{} {}/{}", phase_, completed, total_),
            .processed = completed,
            .total = total_,
            .unit = unit_};
  }

  void update(size_t completed) {
    completed_.store(completed, std::memory_order_relaxed);
  }

 private:
  std::string const label_;
  std::string const phase_;
  std::string const unit_;
  size_t const total_;
  std::atomic<size_t> completed_{0};
};

bool show_nilsimsa_progress(size_t hashes, size_t passes, size_t window) {
  // Avoid flashing contexts for small inputs.
  constexpr uint64_t kMinEstimatedCandidates = 10'000'000;
  return uint64_t{hashes} * passes * (2 * window) >= kMinEstimatedCandidates;
}

struct walk_state {
  std::span<hash_type const* const> hashes;
  std::span<sorted_projection> lists;
  size_t window;
  nilsimsa_progress_context* progress;
  nilsimsa_ordering_result& result;
};

// Force-inlined so that each CPU-specific wrapper below gets its own copy
// of the hot loop with the distance kernel inlined.
template <int (*Distance)(hash_type const&, hash_type const&)>
DWARFS_FORCE_INLINE void walk_impl(walk_state& s) {
  auto const hash_at = [&](size_t rank) -> auto const& {
    return *s.hashes[rank];
  };

  // n itself is the out-of-range position sentinel.
  auto const end = static_cast<rank_type>(s.hashes.size());
  rank_type current = 0;

  for (;;) {
    s.result.order.push_back(current);

    auto const remaining = s.hashes.size() - s.result.order.size();

    // A batched update keeps the hot walk free of per-inode atomics.
    if (s.progress && ((s.result.order.size() & 4095) == 0 || remaining == 0)) {
      s.progress->update(s.result.order.size());
    }

    if (remaining == 0) {
      break;
    }

    rank_type best = invalid_rank;
    int best_distance = std::numeric_limits<int>::max();
    auto const& query = hash_at(current);

    auto consider = [&](rank_type candidate) {
      // Repeated candidates cannot change the distance/rank minimum.
      auto d = Distance(query, hash_at(candidate));
      ++s.result.comparisons;
      if (d < best_distance || (d == best_distance && candidate < best)) {
        best = candidate;
        best_distance = d;
      }
    };

    auto const exact = remaining <= exact_tail;
    // Once in the exact tail, only the first projection is ever consulted.
    auto const num_lists = exact ? size_t{1} : s.lists.size();
    std::array<active_positions::cursor, 32> left, right;

    for (size_t p = 0; p < num_lists; ++p) {
      auto& list = s.lists[p];
      auto const pos = list.position(current, hash_at);
      std::tie(left[p], right[p]) = list.around(pos);
      list.erase(pos);
    }

    auto const limit = exact ? remaining : s.window;

    // Walk each projection independently, keeping cursors local to this step.
    for (size_t p = 0; p < num_lists; ++p) {
      auto const& list = s.lists[p];

      for (size_t i = 0; i < limit; ++i) {
        auto const l = list.prev(left[p], end);
        auto const r = list.next(right[p], end);

        if (l == end && r == end) {
          break;
        }

        if (l != end) {
          consider(list.rank(l));
        }

        if (r != end) {
          consider(list.rank(r));
        }
      }
    }

    assert(best != invalid_rank);
    s.result.total_distance += best_distance;
    current = best;
  }
}

#if defined(__aarch64__)
void walk(walk_state&& s) { walk_impl<distance_neon>(s); }
#else
void walk_portable(walk_state& s) { walk_impl<distance_portable>(s); }

#if defined(__linux__) && defined(__arm__)
void walk_neon(walk_state& s) { walk_impl<distance_neon>(s); }
#elif defined(DWARFS_USE_CPU_FEATURES) && defined(__x86_64__)
__attribute__((__target__("popcnt"))) void walk_popcnt(walk_state& s) {
  walk_impl<distance_portable>(s);
}
#endif

void walk(walk_state&& s) {
#if defined(__linux__) && defined(__arm__)
  if ((getauxval(AT_HWCAP) & HWCAP_NEON) != 0) {
    walk_neon(s);
    return;
  }
#elif defined(DWARFS_USE_CPU_FEATURES) && defined(__x86_64__)
  if (__builtin_cpu_supports("popcnt")) {
    walk_popcnt(s);
    return;
  }
#endif
  walk_portable(s);
}
#endif

} // namespace

/*

Random projections
------------------

Each inode is represented by a 256-bit Nilsimsa fingerprint. Inodes with
identical fingerprints are grouped together first. The remaining fingerprints
are assigned a canonical rank, which also determines the starting point and
makes tie-breaking deterministic.

For each projection, the algorithm picks 64 distinct bit positions from the
256-bit fingerprint, using a pseudorandom sequence. The selected bits form a
64-bit sorting key. Neighbouring fingerprints tend to agree on many bits, so
sorting by several different projections gives nearby fingerprints multiple
chances to end up close to each other. The `passes` option sets the number
of projections. The `seed` option can be used to generate different sequences
of projections, which may yield different orderings.

The projection keys are only needed while building the sorted lists. To keep
sorting fast, a temporary 64-bit value combines the leading part of each key
with its canonical rank. Entries with equal leading parts are sorted again
using their complete projection keys.

Each finished sorted projection stores the following information:

* A packed array of canonical ranks, in projection-key order. Each rank uses
  a width determined by the number of unique fingerprints, rather than a
  fixed 32-bit integer. The array remains unchanged throughout the walk.

* A directory that gives the start of each prefix bucket in the sorted array.
  A bucket contains all ranks whose projection keys begin with the same bits.
  The number of prefix bits is chosen automatically, aiming for about 64 to
  128 ranks per bucket when keys are evenly distributed. The directory has
  one entry for each possible prefix, plus a final end position. The start
  of the next bucket also gives the end of the current one.

* A bitmap of active positions, divided into blocks of 64 positions, with
  links between nonempty blocks. Each block contains a 64-bit mask and two
  block indices for its neighbours. This tracks which ranks are still
  available without changing their positions in the sorted array.

* Optionally, a few bits per canonical rank that help find its position in
  the sorted array. These are controlled by `offset` and described below.

For example, a projection with three prefix bits might look like this:

             bucket 010       bucket 011         bucket 100
           +--------------+------------------+----------+
  position | 0   1   2    | 3   4   5   6    | 7   8    |
  rank     | 18  7   34   | 12  4   27  9    | 3   21   |
           +--------------+------------------+----------+

  directory[010] = 0      directory[011] = 3
  directory[100] = 7      directory[101] = 9

The ranks are the identities of the fingerprints. Different projections
contain the same ranks in different orders. The prefix directory makes
it possible to search a small part of each array without keeping all
the original 64-bit keys.

Walking the projections
-----------------------

The walk starts with canonical rank zero. At each step, the current rank is
found in every projection's sorted array. Its position is located using the
prefix directory and, if enabled, the `offset` hints described below.

The algorithm then looks at the nearest remaining positions in the sorted
array on both sides of the current position. The `window` option controls
how many active entries are examined on each side, in each projection.
For example, with `window=2`:

  projection 1:  ...  17   6  [12]  40   3  ...
  projection 2:  ...   4  31  [12]   9  22  ...
                               |
                          current rank

Here, both projections suggest candidates for rank 12. The candidates may
be different because the two projections use different fingerprint bits.
Entries already chosen earlier in the walk are skipped.

Each candidate is scored using the Hamming distance between the complete
256-bit fingerprints. The candidate with the smallest distance becomes the
next rank in the path. Equal distances are resolved by canonical rank.
This produces a greedy path that tries to keep consecutive fingerprints
similar, which ultimately groups common data and helps the compressor.

The algorithm uses two cursors per projection, one to search backwards and
one to search forwards through the active positions. Each cursor takes the
next set bit from a 64-bit bitmap. When a bitmap has no more active bits in
that direction, the cursor follows a link to the next nonempty block.

A block represents 64 consecutive positions in the sorted rank array. A set
bit means the position is still active, while a cleared bit means that its
rank has already been placed in the output path. Each block also stores
links to the previous and next nonempty blocks.

When the current rank is visited, the algorithm creates the two cursors
around its sorted position and clears the corresponding bit in each
projection. If this was the last active position in a block, that block
is removed from the linked chain by updating its neighbours. The packed
rank array and prefix directory remain unchanged. This keeps removal cheap
and lets subsequent searches skip long runs of already used positions.

When 256 or fewer fingerprints remain, the algorithm considers every
remaining fingerprint to finish the path. Identical fingerprints are then
expanded back into the final inode order.

The `offset` option
-------------------

The walk repeatedly needs to find a canonical rank in each projection's
sorted array. The rank's fingerprint determines its prefix bucket. The
directory provides that bucket's start and end positions, but not the exact
position of the rank within it.

With `offset=0`, the algorithm searches the bucket directly. It scans small
buckets and uses a binary search for larger ones. For the binary search, it
compares the selected fingerprint bits as needed, since the complete sorting
keys are no longer stored.

With `offset=k`, each rank also stores the lowest k bits of its position
*relative to the start of its bucket*. This is equivalent to storing that
relative position modulo 2^k. Thus, only positions with the same remainder
need to be searched.

For example, suppose a bucket begins at position 200, has 12 entries, and
rank 12 is at position 205. With `offset=2`, its relative position is 5, so
the stored hint is `5 mod 4 = 1`:

  bucket positions:  200 201 202 203 204 205 206 207 208 209 210 211
  possible matches:      ^               ^               ^
  ranks at matches:      ?              12               ?

Instead of searching all 12 positions, the lookup only needs to consider
201, 205 and 209. With `offset=4`, the hint would identify position 205
immediately in this bucket. Larger buckets can still require a search, but
the stride between possible matches grows with the number of offset bits.

The default is `offset=8`. Larger values speed up these repeated lookups at
the cost of a few more bits per rank and projection. The setting changes
memory use and lookup time, but not the resulting inode order.

*/

nilsimsa_ordering_result
order_nilsimsa_hashes(std::span<std::array<uint64_t, 4> const* const> hashes,
                      size_t passes, size_t window, size_t offset_bits,
                      uint64_t seed, progress* prog, std::string_view context) {
  auto const n = hashes.size();
  auto const hash_at = [&](size_t rank) -> auto const& {
    return *hashes[rank];
  };

  if (passes == 0 || passes > 32 || window == 0 || window > 128 ||
      offset_bits > 8) {
    throw std::invalid_argument("invalid Nilsimsa ordering parameters");
  }

  if (n > invalid_rank) {
    throw std::length_error("too many Nilsimsa ranks");
  }

  nilsimsa_ordering_result result;

  if (n == 0) {
    return result;
  }

  bool const small = n <= exact_tail;

  if (small) {
    passes = 1;
  }

  auto const bits = static_cast<uint8_t>(std::bit_width(n));

  result.rank_bits = bits;
  result.offset_bits = offset_bits;
  result.key_bits = 64 - bits;

  // Aim for 64--128 ranks per bucket on uniformly distributed prefixes.
  if (!small) {
    result.prefix_bits = std::min(16U, static_cast<unsigned>(bits - 7));
  }

  using clock = std::chrono::steady_clock;

  auto const start = clock::now();

  auto const show_progress =
      prog && !small && show_nilsimsa_progress(n, passes, window);
  auto sorting =
      show_progress ? prog->create_context<nilsimsa_progress_context>(
                          context, "sorting projections", "projections", passes)
                    : std::shared_ptr<nilsimsa_progress_context>{};

  std::vector<sorted_projection> lists;
  lists.reserve(passes);

  {
    std::vector<uint64_t> keys(n);
    uint64_t rng_state = 0x6a09e667f3bcc909ULL + seed;

    for (size_t p = 0; p < passes; ++p) {
      // The 64 KiB lookup table can be too large for small (e.g. musl) thread
      // stacks, so keep it on the heap.
      auto const project = std::make_unique<projection const>(rng_state);

      sort_projection(keys, *project, bits, hash_at);

      auto& list = lists.emplace_back(keys, *project, bits, result.prefix_bits,
                                      result.offset_bits);
      result.projection_bytes += list.capacity_in_bytes();

      if (sorting) {
        sorting->update(p + 1);
      }
    }

    // The sorting buffer is released before reserving the output vector.
    result.peak_scratch_bytes =
        result.projection_bytes + keys.capacity() * sizeof(uint64_t);
  }

  sorting.reset();

  auto ordering = show_progress
                      ? prog->create_context<nilsimsa_progress_context>(
                            context, "ordering unique inodes", "inodes", n)
                      : std::shared_ptr<nilsimsa_progress_context>{};

  result.order = container::packed_int_vector<rank_type>(bits);
  result.order.reserve(n);

  auto const walk_start = clock::now();

  result.build_time = walk_start - start;
  result.peak_scratch_bytes =
      std::max(result.peak_scratch_bytes,
               result.projection_bytes + result.order.capacity_in_bytes());
  walk({.hashes = hashes,
        .lists = lists,
        .window = window,
        .progress = ordering.get(),
        .result = result});

  result.walk_time = clock::now() - walk_start;

  return result;
}

template <typename LoggerPolicy>
class similarity_ordering_ final : public similarity_ordering::impl {
 public:
  using index_value_type = similarity_ordering::index_value_type;
  using index_type = sortable_inode_span::index_type;
  using duplicates_map = std::unordered_map<index_value_type, index_type>;

  similarity_ordering_(logger& lgr, worker_group& wg,
                       similarity_ordering_options const& opts)
      : LOG_PROXY_INIT(lgr)
      , wg_{wg}
      , opts_{opts} {}

  void order_nilsimsa(similarity_element_view& ev, receiver<index_type> rec,
                      index_type index) const override;

 private:
  duplicates_map
  find_duplicates(similarity_element_view const& ev, index_type& index,
                  nilsimsa_preparation_context* preparing) const;

#if defined(DWARFS_DEBUG_SIMILARITY_ORDERING)
  size_t total_distance(similarity_element_view const& ev,
                        index_type const& index) const;
#endif

  void order_impl(receiver<index_type>&& rec, index_type index,
                  similarity_element_view& ev) const;

  LOG_PROXY_DECL(LoggerPolicy);
  worker_group& wg_;
  similarity_ordering_options const opts_;
};

template <typename LoggerPolicy>
auto similarity_ordering_<LoggerPolicy>::find_duplicates(
    similarity_element_view const& ev, index_type& index,
    nilsimsa_preparation_context* preparing) const -> duplicates_map {
  duplicates_map dm;

  {
    auto tt = LOG_TIMED_TRACE;

    // Break ties canonically, so each group of identical hashes starts with
    // its canonical representative and is already in canonical order.
    sort_index(index, [&ev](auto a, auto b) {
      auto const c = ev.get_bits(a) <=> ev.get_bits(b);
      return c != 0 ? c < 0 : ev.order_less(a, b);
    });

    if (preparing) {
      preparing->update(
          nilsimsa_preparation_context::phase::finding_duplicates);
    }

    tt << opts_.context << "sort index of " << index.size() << " elements";
  }

  {
    auto tt = LOG_TIMED_TRACE;

    if (!index.empty()) {
      auto src = index.begin();
      auto dst = src;

      while (++src != index.end()) {
        if (ev.get_bits(*dst) == ev.get_bits(*src)) {
          dm[*dst].push_back(*src);
        } else if (++dst != src) {
          *dst = *src;
        }
      }

      index.erase(++dst, index.end());
    }

    tt << opts_.context << "find duplicates: " << index.size() << " unique / "
       << dm.size() << " groups";
  }

  return dm;
}

#if defined(DWARFS_DEBUG_SIMILARITY_ORDERING)
template <typename LoggerPolicy>
size_t similarity_ordering_<LoggerPolicy>::total_distance(
    similarity_element_view const& ev, index_type const& index) const {
  size_t td = 0;

  if (!index.empty()) {
    auto* prev = &ev.get_bits(index[0]);

    for (size_t i = 1; i < index.size(); ++i) {
      auto& curr = ev.get_bits(index[i]);
      td += distance_portable(*prev, curr);
      prev = &curr;
    }
  }

  return td;
}
#endif

template <typename LoggerPolicy>
void similarity_ordering_<LoggerPolicy>::order_impl(
    receiver<index_type>&& rec, index_type index,
    similarity_element_view& ev) const {
#if defined(DWARFS_DEBUG_SIMILARITY_ORDERING)
  LOG_DEBUG << opts_.context
            << "total distance before ordering: " << total_distance(ev, index);
#endif

  auto const size_hint = index.size();
  auto preparing =
      opts_.prog && size_hint >= 250'000
          ? opts_.prog->create_context<nilsimsa_preparation_context>(
                opts_.context, size_hint)
          : std::shared_ptr<nilsimsa_preparation_context>{};
  auto duplicates = find_duplicates(ev, index, preparing.get());

  if (preparing) {
    preparing->update(nilsimsa_preparation_context::phase::sorting_unique,
                      index.size());
  }

  // Canonical starting order and deterministic tie-breaking.
  sort_index(index, [&ev](auto a, auto b) { return ev.order_less(a, b); });

  if (preparing) {
    preparing->update(nilsimsa_preparation_context::phase::caching);
  }

  nilsimsa_ordering_result res;
  size_t rank_cache_bytes = 0, released_cache_bytes = 0;

  {
    using bitvec_type = similarity_element_view::bitvec_type;
    std::vector<bitvec_type const*> hashes;

    hashes.reserve(index.size());

    for (auto i : index) {
      hashes.push_back(&ev.get_bits(i));
    }

    rank_cache_bytes = hashes.capacity() * sizeof(bitvec_type const*);

    // The pointers refer to inode storage, not the original pointer cache.
    // Only the rank-ordered cache is needed during the projection/walk phase.
    released_cache_bytes = ev.release_hash_cache();

    preparing.reset();

    auto tv = LOG_TIMED_VERBOSE;
    res = order_nilsimsa_hashes(hashes, opts_.passes, opts_.window,
                                opts_.offset_bits, opts_.seed, opts_.prog,
                                opts_.context);
    tv << opts_.context << "nilsimsa ordering: " << index.size()
       << " unique hashes, " << res.comparisons << " comparisons, distance "
       << res.total_distance;
  }

  auto const build_ms =
      std::chrono::duration<double, std::milli>(res.build_time).count();
  auto const walk_ms =
      std::chrono::duration<double, std::milli>(res.walk_time).count();

  LOG_VERBOSE << opts_.context << "nilsimsa ordering phases: build=" << build_ms
              << "ms, walk=" << walk_ms << "ms, rank bits=" << res.rank_bits
              << ", offset bits=" << res.offset_bits
              << ", key bits=" << res.key_bits
              << ", prefix bits=" << res.prefix_bits
              << ", rank cache bytes=" << rank_cache_bytes
              << ", released cache bytes=" << released_cache_bytes
              << ", projection bytes=" << res.projection_bytes
              << ", peak scratch payload bytes=" << res.peak_scratch_bytes;

#if defined(DWARFS_DEBUG_SIMILARITY_ORDERING)
  LOG_DEBUG << opts_.context << "canonical rank order checksum: " << [&] {
    // FNV-1a over little-endian uint32_t ranks
    uint64_t checksum = 14695981039346656037ULL;
    for (uint32_t rank : std::as_const(res.order)) {
      for (unsigned shift = 0; shift < 32; shift += 8) {
        checksum ^= (rank >> shift) & 0xff;
        checksum *= 1099511628211ULL;
      }
    }
    return checksum;
  }();
#endif

  index_type rv;
  rv.reserve(size_hint);

  for (auto rank : std::as_const(res.order)) {
    auto i = static_cast<size_t>(index[rank]);
    rv.push_back(i);

    if (auto it = duplicates.find(i); it != duplicates.end()) {
      for (auto d : it->second) {
        rv.push_back(d);
      }
    }
  }

  LOG_DEBUG << opts_.context
            << "total distance after ordering: " << res.total_distance;

  rec.set_value(std::move(rv));
}

template <typename LoggerPolicy>
void similarity_ordering_<LoggerPolicy>::order_nilsimsa(
    similarity_element_view& ev, receiver<index_type> rec,
    index_type index) const {
  wg_.add_job(
      [this, rec = std::move(rec), idx = std::move(index), &ev] mutable {
        order_impl(std::move(rec), std::move(idx), ev);
      });
}

similarity_ordering::similarity_ordering(
    logger& lgr, internal::worker_group& wg,
    similarity_ordering_options const& opts)
    : impl_{make_unique_logging_object<impl, internal::similarity_ordering_,
                                       default_logger_policy>(lgr, wg, opts)} {}

} // namespace dwarfs::writer::internal
