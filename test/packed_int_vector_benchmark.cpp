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
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <vector>

#include <benchmark/benchmark.h>

#include <dwarfs/binary_literals.h>
#include <dwarfs/internal/compact_packed_int_vector.h>
#include <dwarfs/internal/packed_int_vector.h>
#include <dwarfs/internal/segmented_packed_int_vector.h>

using namespace dwarfs::internal;
using namespace dwarfs::binary_literals;

namespace {

using value_type = uint64_t;
using std_vec = std::vector<value_type>;
using compact_packed_vec = compact_packed_int_vector<value_type>;
using compact_auto_packed_vec = compact_auto_packed_int_vector<value_type>;
using packed_vec = packed_int_vector<value_type>;
using auto_packed_vec = auto_packed_int_vector<value_type>;
using seg_packed_vec = segmented_packed_int_vector<value_type>;

constexpr std::size_t value_bits = std::numeric_limits<value_type>::digits;

constexpr value_type bit_mask(std::size_t bits) {
  if (bits == 0) {
    return 0;
  }
  if (bits >= value_bits) {
    return std::numeric_limits<value_type>::max();
  }
  return (value_type{1} << bits) - 1;
}

uint64_t xorshift64star(uint64_t& state) {
  state ^= state >> 12;
  state ^= state << 25;
  state ^= state >> 27;
  return state * 2685821657736338717ULL;
}

std::vector<value_type> make_values(std::size_t n, std::size_t bits,
                                    uint64_t seed = 0x123456789abcdef0ULL) {
  std::vector<value_type> values(n);
  auto const mask = bit_mask(bits);

  for (std::size_t i = 0; i < n; ++i) {
    values[i] = static_cast<value_type>(xorshift64star(seed)) & mask;
  }

  return values;
}

std::vector<std::size_t>
make_indices(std::size_t n, uint64_t seed = 0xfedcba9876543210ULL) {
  std::vector<std::size_t> indices(n);
  if (n == 0) {
    return indices;
  }

  for (std::size_t i = 0; i < n; ++i) {
    indices[i] = xorshift64star(seed) % n;
  }

  return indices;
}

template <typename Container>
std::size_t storage_bytes(Container const& vec) {
  if constexpr (requires { vec.size_in_bytes(); }) {
    return vec.size_in_bytes();
  } else {
    return vec.size() * sizeof(typename Container::value_type);
  }
}

template <typename Container>
uint64_t sum_sequential(Container const& vec) {
  uint64_t sum = 0;
  for (std::size_t i = 0; i < vec.size(); ++i) {
    sum += vec[i];
  }
  return sum;
}

template <typename Container>
uint64_t
sum_random(Container const& vec, std::vector<std::size_t> const& indices) {
  uint64_t sum = 0;
  for (auto i : indices) {
    sum += vec[i];
  }
  return sum;
}

std_vec
make_std_vec(std::size_t /*bits*/, std::vector<value_type> const& values) {
  return values;
}

compact_packed_vec
make_compact_packed_vec(std::size_t bits,
                        std::vector<value_type> const& values) {
  compact_packed_vec vec(bits);
  vec.reserve(values.size());
  for (auto v : values) {
    vec.push_back(v);
  }
  return vec;
}

compact_auto_packed_vec
make_compact_auto_packed_vec_exact(std::size_t bits,
                                   std::vector<value_type> const& values) {
  compact_auto_packed_vec vec(bits);
  vec.reserve(values.size());
  for (auto v : values) {
    vec.push_back(v);
  }
  return vec;
}

compact_auto_packed_vec
make_compact_auto_packed_vec_growing(std::size_t /*bits*/,
                                     std::vector<value_type> const& values) {
  compact_auto_packed_vec vec(0);
  for (auto v : values) {
    vec.push_back(v);
  }
  return vec;
}

packed_vec
make_packed_vec(std::size_t bits, std::vector<value_type> const& values) {
  packed_vec vec(bits);
  vec.reserve(values.size());
  for (auto v : values) {
    vec.push_back(v);
  }
  return vec;
}

auto_packed_vec
make_auto_packed_vec_exact(std::size_t bits,
                           std::vector<value_type> const& values) {
  auto_packed_vec vec(bits);
  vec.reserve(values.size());
  for (auto v : values) {
    vec.push_back(v);
  }
  return vec;
}

auto_packed_vec
make_auto_packed_vec_growing(std::size_t /*bits*/,
                             std::vector<value_type> const& values) {
  auto_packed_vec vec(0);
  for (auto v : values) {
    vec.push_back(v);
  }
  return vec;
}

seg_packed_vec make_seg_packed_vec(std::size_t /*bits*/,
                                   std::vector<value_type> const& values) {
  seg_packed_vec vec;
  for (auto v : values) {
    vec.push_back(v);
  }
  return vec;
}

template <typename Container>
void set_common_counters(benchmark::State& state, std::size_t bits,
                         Container const& vec) {
  state.counters["bits"] = static_cast<double>(bits);
  state.counters["values"] = static_cast<double>(vec.size());
  state.counters["storage_B"] = static_cast<double>(storage_bytes(vec));
  state.SetItemsProcessed(state.iterations() *
                          static_cast<int64_t>(vec.size()));
  state.SetBytesProcessed(
      state.iterations() *
      static_cast<int64_t>(vec.size() * sizeof(value_type)));
}

template <typename Maker>
void run_build_benchmark(benchmark::State& state, Maker&& make_vec) {
  auto const bits = static_cast<std::size_t>(state.range(0));
  auto const n = static_cast<std::size_t>(state.range(1));
  auto const values = make_values(n, bits);

  auto sample = make_vec(bits, values);

  for (auto _ : state) {
    auto vec = make_vec(bits, values);
    benchmark::DoNotOptimize(vec);
    benchmark::ClobberMemory();
  }

  set_common_counters(state, bits, sample);
}

template <typename Maker>
void run_sequential_read_benchmark(benchmark::State& state, Maker&& make_vec) {
  auto const bits = static_cast<std::size_t>(state.range(0));
  auto const n = static_cast<std::size_t>(state.range(1));
  auto const values = make_values(n, bits);
  auto vec = make_vec(bits, values);

  for (auto _ : state) {
    auto sum = sum_sequential(vec);
    benchmark::DoNotOptimize(sum);
  }

  set_common_counters(state, bits, vec);
}

template <typename Maker>
void run_random_read_benchmark(benchmark::State& state, Maker&& make_vec) {
  auto const bits = static_cast<std::size_t>(state.range(0));
  auto const n = static_cast<std::size_t>(state.range(1));
  auto const values = make_values(n, bits);
  auto const indices = make_indices(n);
  auto vec = make_vec(bits, values);

  for (auto _ : state) {
    auto sum = sum_random(vec, indices);
    benchmark::DoNotOptimize(sum);
  }

  set_common_counters(state, bits, vec);
}

template <typename Container>
void overwrite_all(Container& vec, std::vector<value_type> const& values) {
  for (std::size_t i = 0; i < values.size(); ++i) {
    vec[i] = values[i];
  }
}

template <typename Maker>
void run_overwrite_benchmark(benchmark::State& state, Maker&& make_vec) {
  auto const bits = static_cast<std::size_t>(state.range(0));
  auto const n = static_cast<std::size_t>(state.range(1));
  auto const initial_values = make_values(n, bits, 0x1111111111111111ULL);
  auto const update_values = make_values(n, bits, 0x2222222222222222ULL);
  auto vec = make_vec(bits, initial_values);

  for (auto _ : state) {
    overwrite_all(vec, update_values);
    benchmark::ClobberMemory();
  }

  benchmark::DoNotOptimize(vec);

  set_common_counters(state, bits, vec);
}

template <typename Maker>
void run_sort_benchmark(benchmark::State& state, Maker&& make_vec) {
  auto const bits = static_cast<std::size_t>(state.range(0));
  auto const n = static_cast<std::size_t>(state.range(1));
  auto const values = make_values(n, bits);
  auto vec = make_vec(bits, values);

  for (auto _ : state) {
    std::ranges::sort(vec);
    benchmark::ClobberMemory();
  }

  benchmark::DoNotOptimize(vec);

  set_common_counters(state, bits, vec);
}

// build / push_back
static void bm_build_std_vec(benchmark::State& state) {
  run_build_benchmark(state, make_std_vec);
}

static void bm_build_compact_packed_vec(benchmark::State& state) {
  run_build_benchmark(state, make_compact_packed_vec);
}

static void bm_build_compact_auto_packed_vec_exact(benchmark::State& state) {
  run_build_benchmark(state, make_compact_auto_packed_vec_exact);
}

static void bm_build_compact_auto_packed_vec_growing(benchmark::State& state) {
  run_build_benchmark(state, make_compact_auto_packed_vec_growing);
}

static void bm_build_packed_vec(benchmark::State& state) {
  run_build_benchmark(state, make_packed_vec);
}

static void bm_build_auto_packed_vec_exact(benchmark::State& state) {
  run_build_benchmark(state, make_auto_packed_vec_exact);
}

static void bm_build_auto_packed_vec_growing(benchmark::State& state) {
  run_build_benchmark(state, make_auto_packed_vec_growing);
}

static void bm_build_seg_packed_vec_growing(benchmark::State& state) {
  run_build_benchmark(state, make_seg_packed_vec);
}

// sequential read
static void bm_sum_sequential_std_vec(benchmark::State& state) {
  run_sequential_read_benchmark(state, make_std_vec);
}

static void bm_sum_sequential_compact_packed_vec(benchmark::State& state) {
  run_sequential_read_benchmark(state, make_compact_packed_vec);
}

static void bm_sum_sequential_compact_auto_packed_vec(benchmark::State& state) {
  run_sequential_read_benchmark(state, make_compact_auto_packed_vec_exact);
}

static void bm_sum_sequential_packed_vec(benchmark::State& state) {
  run_sequential_read_benchmark(state, make_packed_vec);
}

static void bm_sum_sequential_auto_packed_vec(benchmark::State& state) {
  run_sequential_read_benchmark(state, make_auto_packed_vec_exact);
}

static void bm_sum_sequential_seg_packed_vec(benchmark::State& state) {
  run_sequential_read_benchmark(state, make_seg_packed_vec);
}

// random read
static void bm_sum_random_std_vec(benchmark::State& state) {
  run_random_read_benchmark(state, make_std_vec);
}

static void bm_sum_random_compact_packed_vec(benchmark::State& state) {
  run_random_read_benchmark(state, make_compact_packed_vec);
}

static void bm_sum_random_compact_auto_packed_vec(benchmark::State& state) {
  run_random_read_benchmark(state, make_compact_auto_packed_vec_exact);
}

static void bm_sum_random_packed_vec(benchmark::State& state) {
  run_random_read_benchmark(state, make_packed_vec);
}

static void bm_sum_random_auto_packed_vec(benchmark::State& state) {
  run_random_read_benchmark(state, make_auto_packed_vec_exact);
}

static void bm_sum_random_seg_packed_vec(benchmark::State& state) {
  run_random_read_benchmark(state, make_seg_packed_vec);
}

// overwrite existing elements
static void bm_overwrite_std_vec(benchmark::State& state) {
  run_overwrite_benchmark(state, make_std_vec);
}

static void bm_overwrite_compact_packed_vec(benchmark::State& state) {
  run_overwrite_benchmark(state, make_compact_packed_vec);
}

static void bm_overwrite_compact_auto_packed_vec(benchmark::State& state) {
  run_overwrite_benchmark(state, make_compact_auto_packed_vec_exact);
}

static void bm_overwrite_packed_vec(benchmark::State& state) {
  run_overwrite_benchmark(state, make_packed_vec);
}

static void bm_overwrite_auto_packed_vec(benchmark::State& state) {
  run_overwrite_benchmark(state, make_auto_packed_vec_exact);
}

static void bm_overwrite_seg_packed_vec(benchmark::State& state) {
  run_overwrite_benchmark(state, make_seg_packed_vec);
}

// sort container elements
static void bm_sort_std_vec(benchmark::State& state) {
  run_sort_benchmark(state, make_std_vec);
}

static void bm_sort_compact_packed_vec(benchmark::State& state) {
  run_sort_benchmark(state, make_compact_packed_vec);
}

static void bm_sort_compact_auto_packed_vec(benchmark::State& state) {
  run_sort_benchmark(state, make_compact_auto_packed_vec_exact);
}

static void bm_sort_packed_vec(benchmark::State& state) {
  run_sort_benchmark(state, make_packed_vec);
}

static void bm_sort_auto_packed_vec(benchmark::State& state) {
  run_sort_benchmark(state, make_auto_packed_vec_exact);
}

static void bm_sort_seg_packed_vec(benchmark::State& state) {
  run_sort_benchmark(state, make_seg_packed_vec);
}

std::vector<value_type> make_worst_case_widening_values(std::size_t n) {
  std::vector<value_type> values;
  values.reserve(n);

  if (n == 0) {
    return values;
  }

  values.push_back(0); // required_bits == 0

  for (std::size_t bits = 1; bits <= value_bits && values.size() < n; ++bits) {
    values.push_back(value_type{1} << (bits - 1)); // required_bits == bits
  }

  while (values.size() < n) {
    values.push_back(std::numeric_limits<value_type>::max());
  }

  return values;
}

template <typename Maker>
void run_worst_case_build_benchmark(benchmark::State& state, Maker&& make_vec) {
  auto const n = static_cast<std::size_t>(state.range(0));
  auto const values = make_worst_case_widening_values(n);

  auto sample = make_vec(value_bits, values);
  state.counters["values"] = static_cast<double>(sample.size());
  state.counters["storage_B"] = static_cast<double>(storage_bytes(sample));
  state.counters["max_bits"] = static_cast<double>(value_bits);
  state.counters["widenings"] =
      static_cast<double>(std::min<std::size_t>(n > 0 ? n - 1 : 0, value_bits));
  state.SetItemsProcessed(state.iterations() *
                          static_cast<int64_t>(sample.size()));
  state.SetBytesProcessed(
      state.iterations() *
      static_cast<int64_t>(sample.size() * sizeof(value_type)));

  for (auto _ : state) {
    auto vec = make_vec(value_bits, values);
    benchmark::DoNotOptimize(vec);
    benchmark::ClobberMemory();
  }
}

static void
bm_build_compact_auto_packed_vec_worst_case_widening(benchmark::State& state) {
  run_worst_case_build_benchmark(state, make_compact_auto_packed_vec_growing);
}

static void
bm_build_compact_auto_packed_vec_worst_case_exact(benchmark::State& state) {
  run_worst_case_build_benchmark(state, make_compact_auto_packed_vec_exact);
}

static void
bm_build_compact_packed_vec_worst_case_reference(benchmark::State& state) {
  run_worst_case_build_benchmark(state, make_compact_packed_vec);
}

static void
bm_build_auto_packed_vec_worst_case_widening(benchmark::State& state) {
  run_worst_case_build_benchmark(state, make_auto_packed_vec_growing);
}

static void bm_build_auto_packed_vec_worst_case_exact(benchmark::State& state) {
  run_worst_case_build_benchmark(state, make_auto_packed_vec_exact);
}

static void bm_build_packed_vec_worst_case_reference(benchmark::State& state) {
  run_worst_case_build_benchmark(state, make_packed_vec);
}

static void bm_build_std_vec_worst_case_reference(benchmark::State& state) {
  run_worst_case_build_benchmark(state, make_std_vec);
}

} // namespace

BENCHMARK(bm_build_std_vec)->ArgsProduct({{5, 13, 17, 31}, {4096, 65536}});
BENCHMARK(bm_build_compact_packed_vec)
    ->ArgsProduct({{5, 13, 17, 31}, {4096, 65536}});
BENCHMARK(bm_build_compact_auto_packed_vec_exact)
    ->ArgsProduct({{5, 13, 17, 31}, {4096, 65536}});
BENCHMARK(bm_build_compact_auto_packed_vec_growing)
    ->ArgsProduct({{5, 13, 17, 31}, {4096, 65536}});
BENCHMARK(bm_build_packed_vec)->ArgsProduct({{5, 13, 17, 31}, {4096, 65536}});
BENCHMARK(bm_build_auto_packed_vec_exact)
    ->ArgsProduct({{5, 13, 17, 31}, {4096, 65536}});
BENCHMARK(bm_build_auto_packed_vec_growing)
    ->ArgsProduct({{5, 13, 17, 31}, {4096, 65536}});
BENCHMARK(bm_build_seg_packed_vec_growing)
    ->ArgsProduct({{5, 13, 17, 31}, {4096, 65536}});

BENCHMARK(bm_sum_sequential_std_vec)
    ->ArgsProduct({{5, 13, 17, 31}, {4096, 65536}});
BENCHMARK(bm_sum_sequential_compact_packed_vec)
    ->ArgsProduct({{5, 13, 17, 31}, {4096, 65536}});
BENCHMARK(bm_sum_sequential_compact_auto_packed_vec)
    ->ArgsProduct({{5, 13, 17, 31}, {4096, 65536}});
BENCHMARK(bm_sum_sequential_packed_vec)
    ->ArgsProduct({{5, 13, 17, 31}, {4096, 65536}});
BENCHMARK(bm_sum_sequential_auto_packed_vec)
    ->ArgsProduct({{5, 13, 17, 31}, {4096, 65536}});
BENCHMARK(bm_sum_sequential_seg_packed_vec)
    ->ArgsProduct({{5, 13, 17, 31}, {4096, 65536}});

BENCHMARK(bm_sum_random_std_vec)->ArgsProduct({{5, 13, 17, 31}, {4096, 65536}});
BENCHMARK(bm_sum_random_compact_packed_vec)
    ->ArgsProduct({{5, 13, 17, 31}, {4096, 65536}});
BENCHMARK(bm_sum_random_compact_auto_packed_vec)
    ->ArgsProduct({{5, 13, 17, 31}, {4096, 65536}});
BENCHMARK(bm_sum_random_packed_vec)
    ->ArgsProduct({{5, 13, 17, 31}, {4096, 65536}});
BENCHMARK(bm_sum_random_auto_packed_vec)
    ->ArgsProduct({{5, 13, 17, 31}, {4096, 65536}});
BENCHMARK(bm_sum_random_seg_packed_vec)
    ->ArgsProduct({{5, 13, 17, 31}, {4096, 65536}});

BENCHMARK(bm_overwrite_std_vec)->ArgsProduct({{5, 13, 17, 31}, {4096, 65536}});
BENCHMARK(bm_overwrite_compact_packed_vec)
    ->ArgsProduct({{5, 13, 17, 31}, {4096, 65536}});
BENCHMARK(bm_overwrite_compact_auto_packed_vec)
    ->ArgsProduct({{5, 13, 17, 31}, {4096, 65536}});
BENCHMARK(bm_overwrite_packed_vec)
    ->ArgsProduct({{5, 13, 17, 31}, {4096, 65536}});
BENCHMARK(bm_overwrite_auto_packed_vec)
    ->ArgsProduct({{5, 13, 17, 31}, {4096, 65536}});
BENCHMARK(bm_overwrite_seg_packed_vec)
    ->ArgsProduct({{5, 13, 17, 31}, {4096, 65536}});

BENCHMARK(bm_sort_std_vec)
    ->ArgsProduct({{5, 13, 17, 31}, {8, 4096, 65536, 4_MiB}});
BENCHMARK(bm_sort_compact_packed_vec)
    ->ArgsProduct({{5, 13, 17, 31}, {8, 4096, 65536, 4_MiB}});
BENCHMARK(bm_sort_compact_auto_packed_vec)
    ->ArgsProduct({{5, 13, 17, 31}, {8, 4096, 65536, 4_MiB}});
BENCHMARK(bm_sort_packed_vec)
    ->ArgsProduct({{5, 13, 17, 31}, {8, 4096, 65536, 4_MiB}});
BENCHMARK(bm_sort_auto_packed_vec)
    ->ArgsProduct({{5, 13, 17, 31}, {8, 4096, 65536, 4_MiB}});
BENCHMARK(bm_sort_seg_packed_vec)
    ->ArgsProduct({{5, 13, 17, 31}, {8, 4096, 65536, 4_MiB}});

BENCHMARK(bm_build_std_vec_worst_case_reference)
    ->Arg(32)
    ->Arg(64)
    ->Arg(128)
    ->Arg(256);

BENCHMARK(bm_build_compact_packed_vec_worst_case_reference)
    ->Arg(32)
    ->Arg(64)
    ->Arg(128)
    ->Arg(256);

BENCHMARK(bm_build_compact_auto_packed_vec_worst_case_exact)
    ->Arg(32)
    ->Arg(64)
    ->Arg(128)
    ->Arg(256);

BENCHMARK(bm_build_compact_auto_packed_vec_worst_case_widening)
    ->Arg(32)
    ->Arg(64)
    ->Arg(128)
    ->Arg(256);

BENCHMARK(bm_build_packed_vec_worst_case_reference)
    ->Arg(32)
    ->Arg(64)
    ->Arg(128)
    ->Arg(256);

BENCHMARK(bm_build_auto_packed_vec_worst_case_exact)
    ->Arg(32)
    ->Arg(64)
    ->Arg(128)
    ->Arg(256);

BENCHMARK(bm_build_auto_packed_vec_worst_case_widening)
    ->Arg(32)
    ->Arg(64)
    ->Arg(128)
    ->Arg(256);

BENCHMARK_MAIN();
