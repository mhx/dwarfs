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

constexpr size_t const max_runs{250};
constexpr size_t const num_runner_threads{16};
constexpr size_t const num_repetitions{4};

using block = std::pair<size_t, size_t>;

// Use std::shared_mutex because folly::SharedMutex might trigger TSAN
template <typename T>
using synchronized = folly::Synchronized<T, std::shared_mutex>;

template <typename T>
using sync_queue = synchronized<std::queue<T>>;

class source {
 public:
  source(size_t id, std::mt19937& delay_rng, std::mt19937& rng,
         size_t max_blocks = 20, double ips = 5000.0)
      : id_{id}
      , blocks_{init_blocks(delay_rng, rng, max_blocks, ips)} {}

  std::tuple<block, bool, double> next() {
    auto idx = idx_++;
    return {std::make_pair(id_, idx), idx_ >= blocks_.size(), blocks_[idx]};
  }

  size_t id() const { return id_; }

  size_t num_blocks() const { return blocks_.size(); }

  std::chrono::nanoseconds total_time() const {
    auto seconds = std::accumulate(begin(blocks_), end(blocks_), 0.0);
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::duration<double>(seconds));
  }

 private:
  static std::vector<double>
  init_blocks(std::mt19937& delay_rng, std::mt19937& rng, size_t max_blocks,
              double ips) {
    std::uniform_int_distribution<> idist(1, max_blocks);
    std::exponential_distribution<> edist(ips);
    std::vector<double> blocks;
    blocks.resize(idist(rng));
    std::generate(begin(blocks), end(blocks), [&] { return edist(delay_rng); });
    return blocks;
  }

  size_t idx_{0};
  size_t id_;
  std::vector<double> blocks_;
};

void emitter(sync_queue<source>& sources,
             dwarfs::block_merger<size_t, block>& merger) {
  for (;;) {
    auto src = sources.withWLock([](auto&& q) {
      std::optional<source> src;

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

      merger.add(blk.first, blk, is_last);

      if (is_last) {
        break;
      }
    }
  }
}

std::vector<block>
do_run(std::mutex& out_mx, size_t run, std::mt19937& delay_rng) {
  std::mt19937 rng(run);
  std::exponential_distribution<> sources_dist(0.1);
  std::exponential_distribution<> threads_dist(0.1);
  std::exponential_distribution<> slots_dist(0.1);
  std::exponential_distribution<> inflight_dist(0.1);
  std::uniform_real_distribution<> speed_dist(0.1, 10.0);
  std::uniform_int_distribution<> merged_queue_dist(0, 1);
  auto const num_sources{std::max<size_t>(1, sources_dist(rng))};
  auto const num_slots{std::max<size_t>(1, slots_dist(rng))};
  auto const num_threads{std::max<size_t>(num_slots, threads_dist(delay_rng))};
  auto const max_in_flight{std::max<size_t>(1, inflight_dist(delay_rng))};
  bool const use_merged_queue{merged_queue_dist(delay_rng) != 0};

  std::vector<size_t> source_ids;
  sync_queue<source> sources;
  std::chrono::nanoseconds total_time{};

  for (size_t i = 0; i < num_sources; ++i) {
    auto src = source(i, delay_rng, rng, 30, 10000.0 * speed_dist(delay_rng));
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

  sync_queue<merged_block_holder<block>> merged_queue;
  std::vector<block> merged;

  dwarfs::multi_queue_block_merger<size_t, block> merger(
      num_slots, max_in_flight, source_ids,
      [&](merged_block_holder<block> holder) {
        if (use_merged_queue) {
          merged_queue.wlock()->emplace(std::move(holder));
        } else {
          merged.emplace_back(std::move(holder.value()));
        }
      });

  std::vector<std::thread> thr;
  std::atomic<bool> running{use_merged_queue};

  std::thread releaser([&] {
    while (running || !merged_queue.rlock()->empty()) {
      std::this_thread::sleep_for(std::chrono::microseconds(10));
      std::vector<merged_block_holder<block>> holders;
      merged_queue.withWLock([&](auto&& q) {
        while (!q.empty()) {
          holders.emplace_back(std::move(q.front()));
          q.pop();
        }
      });
      for (auto& holder : holders) {
        merged.emplace_back(std::move(holder.value()));
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

[[maybe_unused]] void
dump(std::mutex& out_mx, std::vector<block> const& blocks) {
  if constexpr (debuglevel > 1) {
    std::lock_guard lock{out_mx};
    for (size_t i = 0; i < blocks.size(); ++i) {
      if (i > 0) {
        std::cout << ", ";
      }
      auto const& b = blocks[i];
      std::cout << b.first << "." << b.second;
    }
    std::cout << "\n";
  }
}

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
    auto ref = do_run(out_mx, run, delay_rng);
    dump(out_mx, ref);
    for (size_t rep = 0; rep < num_repetitions; ++rep) {
      if constexpr (debuglevel > 0) {
        std::lock_guard lock{out_mx};
        std::cout << "[" << run << "/" << tid << "] test\n";
      }
      auto test = do_run(out_mx, run, delay_rng);
      dump(out_mx, test);
      if (test == ref) {
        ++passes;
      } else {
        fails.wlock()->emplace_back(run);
      }
    }
  }
}

} // namespace

TEST(block_merger, random) {
  std::mutex out_mx;
  std::atomic<size_t> runs{0};
  std::atomic<size_t> passes{0};
  synchronized<std::vector<size_t>> fails;

  std::vector<std::thread> thr;

  for (size_t i = 0; i < num_runner_threads; ++i) {
    thr.emplace_back(
        [&, i] { runner_thread(i, out_mx, runs, max_runs, passes, fails); });
  }

  for (auto& t : thr) {
    t.join();
  }

  EXPECT_EQ(max_runs * num_repetitions, passes);
  EXPECT_TRUE(fails.rlock()->empty()) << folly::join(", ", *fails.rlock());
}
