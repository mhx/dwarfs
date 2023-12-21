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

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <numeric>
#include <queue>
#include <random>
#include <shared_mutex>
#include <thread>
#include <tuple>
#include <vector>

#include <fmt/format.h>
#include <folly/String.h>
#include <folly/Synchronized.h>

#include "dwarfs/multi_queue_block_merger.h"

using namespace dwarfs;

namespace {

constexpr int const debuglevel{0};

constexpr size_t const max_runs_regular{250};
constexpr size_t const max_runs_partial{50};
constexpr size_t const num_runner_threads{16};
constexpr size_t const num_repetitions{4};

struct block {
  static constexpr bool const kIsSized{false};

  block() = default;
  block(size_t src_id, size_t idx, size_t /*sz*/)
      : source_id{src_id}
      , index{idx} {}

  bool operator==(block const&) const = default;
  auto operator<=>(block const&) const = default;

  std::ostream& operator<<(std::ostream& os) const {
    return os << source_id << "." << index;
  }

  size_t source_id;
  size_t index;
};

struct sized_block {
  static constexpr bool const kIsSized{true};

  sized_block() = default;
  sized_block(size_t src_id, size_t idx, size_t sz)
      : source_id{src_id}
      , index{idx}
      , size{sz} {}

  bool operator==(sized_block const&) const = default;
  auto operator<=>(sized_block const&) const = default;

  std::ostream& operator<<(std::ostream& os) const {
    return os << source_id << "." << index << " (" << size << ")";
  }

  size_t source_id{0};
  size_t index{0};
  size_t size{0};
};

class sized_block_merger_policy {
 public:
  explicit sized_block_merger_policy(
      std::vector<size_t>&& worst_case_block_size)
      : worst_case_block_size_{std::move(worst_case_block_size)} {}

  static size_t block_size(sized_block const& blk) { return blk.size; }
  size_t worst_case_source_block_size(size_t source_id) const {
    return worst_case_block_size_[source_id];
  }

 private:
  std::vector<size_t> worst_case_block_size_;
};

template <typename BlockT>
struct timed_release_block {
  std::chrono::steady_clock::time_point when;
  merged_block_holder<BlockT> holder;

  timed_release_block(std::chrono::steady_clock::time_point when,
                      merged_block_holder<BlockT>&& holder)
      : when{when}
      , holder{std::move(holder)} {}

  bool operator<(timed_release_block const& other) const {
    return when > other.when;
  }
};

// Use std::shared_mutex because folly::SharedMutex might trigger TSAN
template <typename T>
using synchronized = folly::Synchronized<T, std::shared_mutex>;

template <typename T>
using sync_queue = synchronized<std::queue<T>>;

template <typename BlockT>
class source {
 public:
  source(size_t id, std::mt19937& delay_rng, std::mt19937& rng,
         size_t max_blocks = 20, double ips = 5000.0, size_t max_size = 10000)
      : id_{id}
      , blocks_{init_blocks(delay_rng, rng, max_blocks, ips, max_size)} {}

  std::tuple<BlockT, bool, double> next() {
    auto idx = idx_++;
    return {BlockT(id_, idx, blocks_[idx].first), idx_ >= blocks_.size(),
            blocks_[idx].second};
  }

  size_t id() const { return id_; }

  size_t num_blocks() const { return blocks_.size(); }

  std::chrono::nanoseconds total_time() const {
    auto seconds = std::accumulate(
        begin(blocks_), end(blocks_), 0.0,
        [](auto const& a, auto const& b) { return a + b.second; });
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::duration<double>(seconds));
  }

 private:
  static std::vector<std::pair<size_t, double>>
  init_blocks(std::mt19937& delay_rng, std::mt19937& rng, size_t max_blocks,
              double ips, size_t max_size) {
    std::uniform_int_distribution<size_t> bdist(1, max_blocks);
    std::uniform_int_distribution<size_t> sdist(BlockT::kIsSized ? 1 : 0,
                                                max_size);
    std::exponential_distribution<> edist(ips);
    std::vector<std::pair<size_t, double>> blocks;
    blocks.resize(bdist(rng));
    std::generate(begin(blocks), end(blocks),
                  [&] { return std::make_pair(sdist(rng), edist(delay_rng)); });
    return blocks;
  }

  size_t idx_{0};
  size_t id_;
  std::vector<std::pair<size_t, double>> blocks_;
};

template <typename BlockMergerT,
          typename BlockT = typename BlockMergerT::block_type>
void emitter(sync_queue<source<BlockT>>& sources, BlockMergerT& merger) {
  for (;;) {
    auto src = sources.withWLock([](auto&& q) {
      std::optional<source<BlockT>> src;

      if (!q.empty()) {
        src = std::move(q.front());
        q.pop();
      }

      return src;
    });

    if (!src) {
      break;
    }

    auto t = std::chrono::steady_clock::now();

    for (;;) {
      auto [blk, is_last, wait] = src->next();

      t += std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::duration<double>(wait));

      std::this_thread::sleep_until(t);

      merger.add(blk.source_id, blk);

      if (is_last) {
        merger.finish(blk.source_id);
        break;
      }
    }
  }
}

template <typename BlockMergerT, bool PartialRelease,
          typename BlockT = typename BlockMergerT::block_type>
std::vector<BlockT>
do_run(std::mutex& out_mx, size_t run, std::mt19937& delay_rng) {
  std::mt19937 rng(run);
  std::exponential_distribution<> sources_dist(0.1);
  std::exponential_distribution<> threads_dist(0.1);
  std::exponential_distribution<> slots_dist(0.1);
  std::exponential_distribution<> inflight_dist(BlockT::kIsSized ? 0.00001
                                                                 : 0.1);
  std::uniform_real_distribution<> speed_dist(0.1, 10.0);
  std::uniform_int_distribution<> merged_queue_dist(0, 1);
  std::uniform_int_distribution<> worst_case_size_dist(1, 10000);
  std::uniform_int_distribution<> release_after_us_dist(1, 10000);
  std::uniform_int_distribution<> partial_release_repeat_dist(0, 2);
  auto const num_sources{std::max<size_t>(1, sources_dist(rng))};
  auto const num_slots{std::max<size_t>(1, slots_dist(rng))};
  auto const num_threads{std::max<size_t>(num_slots, threads_dist(delay_rng))};
  auto const max_in_flight{
      std::max<size_t>(BlockT::kIsSized ? 10000 : 1, inflight_dist(delay_rng))};
  bool const use_merged_queue{merged_queue_dist(delay_rng) != 0};

  std::vector<size_t> source_ids;
  sync_queue<source<BlockT>> sources;
  std::chrono::nanoseconds total_time{};

  std::vector<size_t> worst_case_block_size;

  for (size_t i = 0; i < num_sources; ++i) {
    size_t worst_case_size{0};

    if constexpr (BlockT::kIsSized) {
      worst_case_size = worst_case_size_dist(rng);
      worst_case_block_size.emplace_back(worst_case_size);
    }

    auto src = source<BlockT>(i, delay_rng, rng, 30,
                              10000.0 * speed_dist(delay_rng), worst_case_size);
    total_time += src.total_time();
    source_ids.emplace_back(src.id());
    sources.wlock()->emplace(std::move(src));
  }

  auto config =
      fmt::format("sources: {}, slots: {}, threads: {}, max in flight: {}",
                  num_sources, num_slots, num_threads, max_in_flight);

  if constexpr (debuglevel > 0) {
    std::lock_guard lock{out_mx};
    std::cout << config << "\n";
  }

  synchronized<std::vector<timed_release_block<BlockT>>> merged_queue;
  std::vector<BlockT> merged;

  auto merge_cb = [&](merged_block_holder<BlockT> holder) {
    merged.emplace_back(std::move(holder.value()));

    if (use_merged_queue) {
      if constexpr (PartialRelease) {
        auto when = std::chrono::steady_clock::now() +
                    std::chrono::microseconds(release_after_us_dist(delay_rng));
        merged_queue.withWLock([&](auto&& q) {
          q.emplace_back(when, std::move(holder));
          std::push_heap(begin(q), end(q));
        });
      } else {
        merged_queue.withWLock([&](auto&& q) {
          q.emplace_back(std::chrono::steady_clock::time_point{},
                         std::move(holder));
        });
      }
    }
  };

  BlockMergerT merger;

  if constexpr (BlockT::kIsSized) {
    merger = BlockMergerT(
        num_slots, max_in_flight, source_ids, std::move(merge_cb),
        sized_block_merger_policy{std::move(worst_case_block_size)});
  } else {
    merger =
        BlockMergerT(num_slots, max_in_flight, source_ids, std::move(merge_cb));
  }

  std::vector<std::thread> thr;
  std::atomic<bool> running{use_merged_queue};

  std::thread releaser([&] {
    std::mt19937 partial_rng(run);

    while (running || !merged_queue.rlock()->empty()) {
      auto now = std::chrono::steady_clock::now();
      std::chrono::steady_clock::time_point next;
      std::vector<merged_block_holder<BlockT>> holders;

      merged_queue.withWLock([&](auto&& q) {
        while (!q.empty()) {
          if constexpr (PartialRelease) {
            std::pop_heap(begin(q), end(q));
          }
          auto& item = q.back();
          if constexpr (PartialRelease) {
            if (item.when > now) {
              next = item.when;
              break;
            }
          }
          holders.emplace_back(std::move(item.holder));
          q.pop_back();
        }
      });

      if constexpr (PartialRelease) {
        std::vector<merged_block_holder<BlockT>> partial;

        for (auto& h : holders) {
          if (partial_release_repeat_dist(partial_rng) > 0) {
            auto& size = h.value().size;
            if (size > 10) {
              auto to_release = size / 2;
              size -= to_release;
              h.release_partial(to_release);
              partial.emplace_back(std::move(h));
              continue;
            }
          }
        }

        merged_queue.withWLock([&](auto&& q) {
          for (auto& h : partial) {
            auto when = now + std::chrono::microseconds(
                                  release_after_us_dist(partial_rng));
            q.emplace_back(when, std::move(h));
            std::push_heap(begin(q), end(q));
          }
        });
      }

      holders.clear();

      if constexpr (PartialRelease) {
        std::this_thread::sleep_until(next);
      } else {
        std::this_thread::sleep_for(std::chrono::microseconds(10));
      }
    }
  });

  auto t0 = std::chrono::steady_clock::now();

  for (size_t i = 0; i < num_threads; ++i) {
    thr.emplace_back([&] { emitter(sources, merger); });
  }

  for (auto& t : thr) {
    t.join();
  }

  running = false;
  releaser.join();

  auto t1 = std::chrono::steady_clock::now();

  auto elapsed = num_threads * (t1 - t0);
  auto efficiency =
      std::chrono::duration_cast<std::chrono::duration<double>>(total_time)
          .count() /
      std::chrono::duration_cast<std::chrono::duration<double>>(elapsed)
          .count();

  if constexpr (debuglevel > 0) {
    std::lock_guard lock{out_mx};
    std::cout << config
              << fmt::format(" => efficiency: {:.2f}%\n", 100.0 * efficiency);
  }

  return merged;
}

template <typename BlockT>
[[maybe_unused]] void
dump(std::mutex& out_mx, std::vector<BlockT> const& blocks) {
  if constexpr (debuglevel > 1) {
    std::lock_guard lock{out_mx};
    for (size_t i = 0; i < blocks.size(); ++i) {
      if (i > 0) {
        std::cout << ", ";
      }
      std::cout << blocks[i];
    }
    std::cout << "\n";
  }
}

template <typename BlockMergerT, bool PartialRelease>
void runner_thread(size_t tid, std::mutex& out_mx, std::atomic<size_t>& runs,
                   size_t const max_runs, std::atomic<size_t>& passes,
                   synchronized<std::vector<size_t>>& fails) {
  std::mt19937 delay_rng(tid);

  for (;;) {
    auto run = runs++;
    if (run >= max_runs) {
      break;
    }
    if constexpr (debuglevel > 0) {
      std::lock_guard lock{out_mx};
      std::cout << "[" << run << "/" << tid << "] ref\n";
    }
    auto ref = do_run<BlockMergerT, PartialRelease>(out_mx, run, delay_rng);
    dump(out_mx, ref);
    for (size_t rep = 0; rep < num_repetitions; ++rep) {
      if constexpr (debuglevel > 0) {
        std::lock_guard lock{out_mx};
        std::cout << "[" << run << "/" << tid << "] test\n";
      }
      auto test = do_run<BlockMergerT, PartialRelease>(out_mx, run, delay_rng);
      dump(out_mx, test);
      if (test == ref) {
        ++passes;
      } else {
        fails.wlock()->emplace_back(run);
      }
    }
  }
}

template <typename BlockMergerT, bool PartialRelease = false>
std::tuple<size_t, std::vector<size_t>>
block_merger_test(size_t const max_runs) {
  std::mutex out_mx;
  std::atomic<size_t> runs{0};
  std::atomic<size_t> passes{0};
  synchronized<std::vector<size_t>> fails;

  std::vector<std::thread> thr;

  for (size_t i = 0; i < num_runner_threads; ++i) {
    thr.emplace_back([&, i] {
      runner_thread<BlockMergerT, PartialRelease>(i, out_mx, runs, max_runs,
                                                  passes, fails);
    });
  }

  for (auto& t : thr) {
    t.join();
  }

  return {passes.load(), *fails.rlock()};
}

} // namespace

TEST(block_merger, random) {
  using merger_type = dwarfs::multi_queue_block_merger<size_t, block>;

  auto [passes, fails] = block_merger_test<merger_type>(max_runs_regular);

  EXPECT_EQ(max_runs_regular * num_repetitions, passes);
  EXPECT_TRUE(fails.empty()) << folly::join(", ", fails);
}

TEST(block_merger, random_sized) {
  using merger_type =
      dwarfs::multi_queue_block_merger<size_t, sized_block,
                                       sized_block_merger_policy>;

  auto [passes, fails] = block_merger_test<merger_type>(max_runs_regular);

  EXPECT_EQ(max_runs_regular * num_repetitions, passes);
  EXPECT_TRUE(fails.empty()) << folly::join(", ", fails);
}

TEST(block_merger, random_sized_partial) {
  using merger_type =
      dwarfs::multi_queue_block_merger<size_t, sized_block,
                                       sized_block_merger_policy>;

  auto [passes, fails] = block_merger_test<merger_type, true>(max_runs_partial);

  EXPECT_EQ(max_runs_partial * num_repetitions, passes);
  EXPECT_TRUE(fails.empty()) << folly::join(", ", fails);
}
