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
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <array>
#include <numeric>
#include <random>
#include <unordered_map>

#include <benchmark/benchmark.h>

#include <dwarfs/sorted_array_map.h>

namespace {

using namespace dwarfs;

constexpr size_t LookupCount = 4096;

template <typename T, size_t N>
constexpr std::array<std::pair<T, T>, N> make_pairs() {
  std::array<std::pair<T, T>, N> pairs{};
  for (size_t i = 0; i < N; ++i) {
    pairs[i].first = pairs[i].second = i;
  }
  return pairs;
}

template <size_t N>
constexpr auto sa_map = sorted_array_map{make_pairs<int, N>()};

std::vector<int> random_vector(int min, int max, size_t count) {
  static std::mt19937 gen{42};
  std::uniform_int_distribution<int> dist{min, max};
  std::vector<int> v(count);
  std::generate(v.begin(), v.end(), [&] { return dist(gen); });
  return v;
}

template <size_t N>
void lookup_constexpr(::benchmark::State& state) {
  auto random = random_vector(0, N - 1, LookupCount);
  for (auto _ : state) {
    for (int k : random) {
      auto v = sa_map<N>.at(k);
      benchmark::DoNotOptimize(v);
    }
  }
  state.SetItemsProcessed(state.iterations() * LookupCount);
}

template <size_t N>
void lookup_runtime(::benchmark::State& state) {
  auto random = random_vector(0, N - 1, LookupCount);
  sorted_array_map<int, int, N> map{make_pairs<int, N>()};

  for (auto _ : state) {
    for (int k : random) {
      auto v = map.at(k);
      benchmark::DoNotOptimize(v);
    }
  }
  state.SetItemsProcessed(state.iterations() * LookupCount);
}

template <size_t N>
void lookup_unordered_map(::benchmark::State& state) {
  auto random = random_vector(0, N - 1, LookupCount);
  std::unordered_map<int, int> map;
  for (size_t i = 0; i < N; ++i) {
    map[i] = i;
  }

  for (auto _ : state) {
    for (int k : random) {
      auto v = map.at(k);
      benchmark::DoNotOptimize(v);
    }
  }
  state.SetItemsProcessed(state.iterations() * LookupCount);
}

} // namespace

BENCHMARK(lookup_constexpr<2>);
BENCHMARK(lookup_constexpr<4>);
BENCHMARK(lookup_constexpr<8>);
BENCHMARK(lookup_constexpr<16>);
BENCHMARK(lookup_constexpr<32>);
BENCHMARK(lookup_constexpr<64>);
BENCHMARK(lookup_constexpr<128>);
BENCHMARK(lookup_constexpr<256>);
BENCHMARK(lookup_constexpr<512>);
BENCHMARK(lookup_constexpr<1024>);
BENCHMARK(lookup_constexpr<2048>);
BENCHMARK(lookup_constexpr<4096>);
BENCHMARK(lookup_constexpr<8192>);

BENCHMARK(lookup_runtime<2>);
BENCHMARK(lookup_runtime<4>);
BENCHMARK(lookup_runtime<8>);
BENCHMARK(lookup_runtime<16>);
BENCHMARK(lookup_runtime<32>);
BENCHMARK(lookup_runtime<64>);
BENCHMARK(lookup_runtime<128>);
BENCHMARK(lookup_runtime<256>);
BENCHMARK(lookup_runtime<512>);
BENCHMARK(lookup_runtime<1024>);
BENCHMARK(lookup_runtime<2048>);
BENCHMARK(lookup_runtime<4096>);
BENCHMARK(lookup_runtime<8192>);

BENCHMARK(lookup_unordered_map<2>);
BENCHMARK(lookup_unordered_map<4>);
BENCHMARK(lookup_unordered_map<8>);
BENCHMARK(lookup_unordered_map<16>);
BENCHMARK(lookup_unordered_map<32>);
BENCHMARK(lookup_unordered_map<64>);
BENCHMARK(lookup_unordered_map<128>);
BENCHMARK(lookup_unordered_map<256>);
BENCHMARK(lookup_unordered_map<512>);
BENCHMARK(lookup_unordered_map<1024>);
BENCHMARK(lookup_unordered_map<2048>);
BENCHMARK(lookup_unordered_map<4096>);
BENCHMARK(lookup_unordered_map<8192>);

BENCHMARK_MAIN();
