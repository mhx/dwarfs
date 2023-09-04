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

#include <array>
#include <bit>
#include <cstdint>
#include <random>
#include <vector>

#include <benchmark/benchmark.h>

#include "dwarfs/compiler.h"
#include "dwarfs/nilsimsa.h"

#include "test_helpers.h"
#include "test_strings.h"

namespace {

template <typename T, size_t N>
int distance(std::array<T, N> const& a, std::array<T, N> const& b) {
  int d = 0;
  for (size_t i = 0; i < N; ++i) {
    d += std::popcount(a[i] ^ b[i]);
  }
  return d;
}

#ifdef DWARFS_MULTIVERSIONING
#ifdef __clang__
__attribute__((target_clones("avx512vpopcntdq", "popcnt", "default")))
#else
__attribute__((target_clones("popcnt", "default")))
#endif
#endif
int distance(std::array<uint64_t, 4> const& a, std::array<uint64_t, 4> const& b) {
  return distance<uint64_t, 4>(a, b);
}

void nilsimsa_distance(::benchmark::State& state) {
  std::independent_bits_engine<std::mt19937_64,
                               std::numeric_limits<uint64_t>::digits, uint64_t>
      rng;
  static constexpr unsigned const kNumData{1024};
  std::vector<std::array<uint64_t, 4>> data(kNumData);
  for (auto& a : data) {
    std::generate(begin(a), end(a), std::ref(rng));
  }
  unsigned i{0}, k{1};
  int d;

  for (auto _ : state) {
    ::benchmark::DoNotOptimize(
        d = distance(data[i++ % kNumData], data[k++ % kNumData]));
  }
}

void nilsimsa_update(::benchmark::State& state) {
  std::independent_bits_engine<std::mt19937_64,
                               std::numeric_limits<uint8_t>::digits, uint16_t>
      rng;
  static constexpr unsigned const kNumData{8 * 1024 * 1024};
  std::vector<uint8_t> data(kNumData);
  std::generate(begin(data), end(data), std::ref(rng));

  dwarfs::nilsimsa s;

  for (auto _ : state) {
    s.update(data.data(), data.size());
  }
}

} // namespace

BENCHMARK(nilsimsa_distance);
BENCHMARK(nilsimsa_update);

BENCHMARK_MAIN();
