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
#include <bit>
#include <cstdint>
#include <random>
#include <vector>

#include <benchmark/benchmark.h>

#include <dwarfs/compiler.h>

#include <dwarfs/writer/internal/nilsimsa.h>

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

#if defined(DWARFS_USE_CPU_FEATURES) && defined(__x86_64__)
#define DWARFS_USE_POPCNT
#endif

enum class cpu_feature {
  none,
  popcnt,
};

cpu_feature detect_cpu_feature() {
#ifdef DWARFS_USE_POPCNT
  static cpu_feature const feature = [] {
    if (__builtin_cpu_supports("popcnt")) {
      return cpu_feature::popcnt;
    }
    return cpu_feature::none;
  }();
  return feature;
#else
  return cpu_feature::none;
#endif
}

template <typename Fn, typename... Args>
decltype(auto) cpu_dispatch(Args&&... args) {
#ifdef DWARFS_USE_POPCNT
  auto feature = detect_cpu_feature();
  switch (feature) {
  case cpu_feature::popcnt:
    return Fn::template call<cpu_feature::popcnt>(std::forward<Args>(args)...);
  default:
    break;
  }
#endif
  return Fn::template call<cpu_feature::none>(std::forward<Args>(args)...);
}

int distance_default(std::array<uint64_t, 4> const& a,
                     std::array<uint64_t, 4> const& b) {
  return distance<uint64_t, 4>(a, b);
}

#ifdef DWARFS_USE_POPCNT
__attribute__((__target__("popcnt"))) int
distance_popcnt(std::array<uint64_t, 4> const& a,
                std::array<uint64_t, 4> const& b) {
  return distance<uint64_t, 4>(a, b);
}
#endif

struct distance_cpu {
  template <cpu_feature CpuFeature>
  static int
  call(std::array<uint64_t, 4> const& a, std::array<uint64_t, 4> const& b) {
#ifdef DWARFS_USE_POPCNT
    if constexpr (CpuFeature == cpu_feature::popcnt) {
      return distance_popcnt(a, b);
    }
#endif
    return distance_default(a, b);
  }
};

int distance(std::array<uint64_t, 4> const& a,
             std::array<uint64_t, 4> const& b) {
  return cpu_dispatch<distance_cpu>(a, b);
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

#ifdef DWARFS_USE_POPCNT
void nilsimsa_distance_cpu(::benchmark::State& state) {
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

  switch (detect_cpu_feature()) {
  case cpu_feature::popcnt:
    for (auto _ : state) {
      ::benchmark::DoNotOptimize(
          d = distance_cpu::template call<cpu_feature::popcnt>(
              data[i++ % kNumData], data[k++ % kNumData]));
    }
    break;
  default:
    for (auto _ : state) {
      ::benchmark::DoNotOptimize(
          d = distance_cpu::template call<cpu_feature::none>(
              data[i++ % kNumData], data[k++ % kNumData]));
    }
    break;
  }
}
#endif

void nilsimsa_update(::benchmark::State& state) {
  std::independent_bits_engine<std::mt19937_64,
                               std::numeric_limits<uint8_t>::digits, uint16_t>
      rng;
  static constexpr unsigned const kNumData{8 * 1024 * 1024};
  std::vector<uint8_t> data(kNumData);
  std::generate(begin(data), end(data), std::ref(rng));

  dwarfs::writer::internal::nilsimsa s;

  for (auto _ : state) {
    s.update(data.data(), data.size());
  }
}

} // namespace

BENCHMARK(nilsimsa_distance);
#ifdef DWARFS_USE_POPCNT
BENCHMARK(nilsimsa_distance_cpu);
#endif
BENCHMARK(nilsimsa_update);

BENCHMARK_MAIN();
