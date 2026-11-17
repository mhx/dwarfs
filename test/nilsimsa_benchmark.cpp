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
#include <array>
#include <chrono>
#include <numeric>
#include <random>
#include <unordered_map>

#include <benchmark/benchmark.h>

#include <dwarfs/writer/internal/nilsimsa.h>
#include <dwarfs/writer/internal/similarity_ordering.h>

namespace {

using namespace dwarfs::writer::internal;

std::vector<uint8_t> random_byte_vector(size_t len) {
  static std::mt19937 g{42};
  static std::uniform_int_distribution<uint16_t> d{0, 255};
  std::vector<uint8_t> v(len);
  std::ranges::generate(v, [&] { return d(g); });
  return v;
}

template <size_t N>
void nilsimsa_bytes(::benchmark::State& state) {
  auto input = random_byte_vector(N);

  for (auto _ : state) {
    nilsimsa ns;
    nilsimsa::hash_type h;
    ns.update(input.data(), input.size());
    ns.finalize(h);
    benchmark::DoNotOptimize(h);
  }

  state.SetItemsProcessed(state.iterations() * N);
}

void nilsimsa_order(::benchmark::State& state) {
  auto const n = static_cast<size_t>(state.range(0));
  auto const passes = static_cast<size_t>(state.range(1));
  auto const window = static_cast<size_t>(state.range(2));
  auto const related = state.range(3) != 0;
  auto const offset_bits = static_cast<size_t>(state.range(4));
  std::mt19937_64 rng{42};
  std::vector<nilsimsa::hash_type> bases(500), hashes(n);

  for (auto& hash : bases) {
    for (auto& word : hash) {
      word = rng();
    }
  }

  for (size_t i = 0; i < n; ++i) {
    auto& hash = hashes[i];

    if (!related) {
      for (auto& word : hash) {
        word = rng();
      }
      continue;
    }

    hash = bases[i % bases.size()];

    for (size_t k = 1 + rng() % 40; k > 0; --k) {
      auto bit = rng() % 256;
      hash[bit / 64] ^= uint64_t{1} << (bit % 64);
    }
  }

  std::sort(hashes.begin(), hashes.end());

  hashes.erase(std::unique(hashes.begin(), hashes.end()), hashes.end());
  std::shuffle(hashes.begin(), hashes.end(), rng);

  std::vector<nilsimsa::hash_type const*> ptrs;
  ptrs.reserve(hashes.size());

  for (auto const& hash : hashes) {
    ptrs.push_back(&hash);
  }

  uint64_t comparisons = 0, total_distance = 0;
  double build_ms = 0, walk_ms = 0;
  size_t projection_bytes = 0, scratch_bytes = 0;

  for (auto _ : state) {
    auto result = order_nilsimsa_hashes(ptrs, passes, window, offset_bits);
    build_ms +=
        std::chrono::duration<double, std::milli>(result.build_time).count();
    walk_ms +=
        std::chrono::duration<double, std::milli>(result.walk_time).count();
    projection_bytes = result.projection_bytes;
    scratch_bytes = result.peak_scratch_bytes;
    comparisons = result.comparisons;
    total_distance = result.total_distance;
    benchmark::DoNotOptimize(result.order);
  }

  state.SetItemsProcessed(state.iterations() * hashes.size());
  state.counters["build_ms"] = build_ms / state.iterations();
  state.counters["walk_ms"] = walk_ms / state.iterations();
  state.counters["projection_bytes"] = static_cast<double>(projection_bytes);
  state.counters["scratch_bytes"] = static_cast<double>(scratch_bytes);
  state.counters["comparisons"] = static_cast<double>(comparisons);
  state.counters["mean_distance"] =
      static_cast<double>(total_distance) / (hashes.size() - 1);
}

} // namespace

BENCHMARK(nilsimsa_bytes<32>);
BENCHMARK(nilsimsa_bytes<1024>);
BENCHMARK(nilsimsa_bytes<1024 * 32>);
BENCHMARK(nilsimsa_bytes<1024 * 1024>);

BENCHMARK(nilsimsa_order)
    ->Args({10000, 8, 16, 1, 4})
    ->Args({100000, 8, 16, 1, 4})
    ->Args({100000, 24, 32, 1, 4})
    ->Args({1000000, 8, 16, 1, 4})
    ->Args({100000, 8, 16, 0, 0})
    ->Args({100000, 8, 16, 0, 2})
    ->Args({100000, 8, 16, 0, 4})
    ->Args({100000, 8, 16, 0, 8})
    ->Args({100000, 24, 32, 0, 4})
    ->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();
