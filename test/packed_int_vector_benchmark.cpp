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
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <benchmark/benchmark.h>

#include <dwarfs/binary_literals.h>
#include <dwarfs/container/compact_packed_int_vector.h>
#include <dwarfs/container/packed_int_vector.h>
#include <dwarfs/container/segmented_packed_int_vector.h>

using namespace dwarfs::container;
using namespace dwarfs::binary_literals;

namespace {

using value_type = uint64_t;
using std_vec = std::vector<value_type>;
using compact_packed_vec = compact_packed_int_vector<value_type>;
using compact_auto_packed_vec = compact_auto_packed_int_vector<value_type>;
using packed_vec = packed_int_vector<value_type>;
using auto_packed_vec = auto_packed_int_vector<value_type>;
using seg_packed_vec = segmented_packed_int_vector<value_type>;

// tuple element types:
// - tup2: small tuple, shared underlying type (uint32_t)
// - tup16: large tuple, shared underlying type (uint16_t); this is where
//   the per-access field-offset cost is most visible
// - tupmix: mixed field types, forcing the storage selector onto the
//   uint8_t underlying-type fallback (different container::bit_view path)
using tup2 = std::tuple<uint32_t, uint32_t>;
using tup16 =
    std::tuple<uint16_t, uint16_t, uint16_t, uint16_t, uint16_t, uint16_t,
               uint16_t, uint16_t, uint16_t, uint16_t, uint16_t, uint16_t,
               uint16_t, uint16_t, uint16_t, uint16_t>;
using tupmix = std::tuple<uint8_t, uint16_t, uint32_t, uint64_t>;

using compact_tup2_vec = compact_packed_int_vector<tup2>;
using compact_tup16_vec = compact_packed_int_vector<tup16>;
using compact_tupmix_vec = compact_packed_int_vector<tupmix>;
using packed_tup16_vec = packed_int_vector<tup16>;
using std_tup16_vec = std::vector<tup16>;

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

template <typename T>
struct is_tuple : std::false_type {};

template <typename... Ts>
struct is_tuple<std::tuple<Ts...>> : std::true_type {};

// A random value with (per-field) width `bits`, clamped to the width of
// each field type.
template <typename T>
T make_random_value(uint64_t& seed, std::size_t bits) {
  if constexpr (is_tuple<T>::value) {
    return [&]<std::size_t... I>(std::index_sequence<I...>) {
      return T{make_random_value<std::tuple_element_t<I, T>>(seed, bits)...};
    }(std::make_index_sequence<std::tuple_size_v<T>>{});
  } else {
    constexpr std::size_t digits = std::numeric_limits<T>::digits;
    return static_cast<T>(xorshift64star(seed) &
                          bit_mask(std::min(bits, digits)));
  }
}

template <typename T = value_type>
std::vector<T> make_values(std::size_t n, std::size_t bits,
                           uint64_t seed = 0x123456789abcdef0ULL) {
  std::vector<T> values(n);

  for (auto& v : values) {
    v = make_random_value<T>(seed, bits);
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

template <typename T>
uint64_t value_sum(T const& v) {
  if constexpr (is_tuple<T>::value) {
    return std::apply(
        [](auto... f) {
          return (uint64_t{0} + ... + static_cast<uint64_t>(f));
        },
        v);
  } else {
    return static_cast<uint64_t>(v);
  }
}

// Uniform per-field widths, clamped to each field's maximum. Works for
// both scalar and tuple element types.
template <typename Vec>
typename Vec::widths_type uniform_widths(std::size_t bits) {
  auto widths = Vec::max_widths();
  for (auto& b : widths) {
    b = static_cast<std::uint8_t>(
        std::min<std::size_t>(bits, static_cast<std::size_t>(b)));
  }
  return widths;
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
    sum += value_sum(vec[i]);
  }
  return sum;
}

template <typename Container>
uint64_t
sum_random(Container const& vec, std::vector<std::size_t> const& indices) {
  uint64_t sum = 0;
  for (auto i : indices) {
    sum += value_sum(vec[i]);
  }
  return sum;
}

template <typename Container>
void overwrite_all(Container& vec,
                   std::vector<typename Container::value_type> const& values) {
  for (std::size_t i = 0; i < values.size(); ++i) {
    vec[i] = values[i];
  }
}

// ---------------------------------------------------------------------
// makers
//
// Each maker exposes `container_type` and `element_type` and builds a
// container from (bits, values). The run_*() templates use element_type
// to generate input data of the right type.
// ---------------------------------------------------------------------

template <typename T = value_type>
struct std_vec_maker {
  using container_type = std::vector<T>;
  using element_type = T;

  container_type
  operator()(std::size_t /*bits*/, std::vector<T> const& values) const {
    return values;
  }
};

// exact widths, reserve(), then push_back()
template <typename Vec>
struct push_back_maker {
  using container_type = Vec;
  using element_type = typename Vec::value_type;

  Vec operator()(std::size_t bits,
                 std::vector<element_type> const& values) const {
    Vec vec(uniform_widths<Vec>(bits));
    vec.reserve(values.size());
    for (auto const& v : values) {
      vec.push_back(v);
    }
    return vec;
  }
};

// zero widths, push_back() with automatic widening (auto strategy only)
template <typename Vec>
struct growing_maker {
  using container_type = Vec;
  using element_type = typename Vec::value_type;

  Vec operator()(std::size_t /*bits*/,
                 std::vector<element_type> const& values) const {
    Vec vec;
    for (auto const& v : values) {
      vec.push_back(v);
    }
    return vec;
  }
};

// exact widths, then bulk insert via the iterator-pair insert()
// (exercises insert_known_n / write_value with hoisted geometry)
template <typename Vec>
struct bulk_insert_maker {
  using container_type = Vec;
  using element_type = typename Vec::value_type;

  Vec operator()(std::size_t bits,
                 std::vector<element_type> const& values) const {
    Vec vec(uniform_widths<Vec>(bits));
    vec.insert(vec.begin(), values.begin(), values.end());
    return vec;
  }
};

struct seg_maker {
  using container_type = seg_packed_vec;
  using element_type = value_type;

  seg_packed_vec operator()(std::size_t /*bits*/,
                            std::vector<value_type> const& values) const {
    seg_packed_vec vec;
    for (auto v : values) {
      vec.push_back(v);
    }
    return vec;
  }
};

// ---------------------------------------------------------------------
// benchmark runners
// ---------------------------------------------------------------------

template <typename Container>
void set_common_counters(benchmark::State& state, std::size_t bits,
                         Container const& vec) {
  using element_type = typename Container::value_type;
  state.counters["bits"] = static_cast<double>(bits);
  state.counters["values"] = static_cast<double>(vec.size());
  state.counters["storage_B"] = static_cast<double>(storage_bytes(vec));
  state.SetItemsProcessed(state.iterations() *
                          static_cast<int64_t>(vec.size()));
  state.SetBytesProcessed(
      state.iterations() *
      static_cast<int64_t>(vec.size() * sizeof(element_type)));
}

template <typename Maker>
void run_build_benchmark(benchmark::State& state, Maker&& make_vec) {
  using element_type = typename std::decay_t<Maker>::element_type;
  auto const bits = static_cast<std::size_t>(state.range(0));
  auto const n = static_cast<std::size_t>(state.range(1));
  auto const values = make_values<element_type>(n, bits);

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
  using element_type = typename std::decay_t<Maker>::element_type;
  auto const bits = static_cast<std::size_t>(state.range(0));
  auto const n = static_cast<std::size_t>(state.range(1));
  auto const values = make_values<element_type>(n, bits);
  auto vec = make_vec(bits, values);

  for (auto _ : state) {
    auto sum = sum_sequential(vec);
    benchmark::DoNotOptimize(sum);
  }

  set_common_counters(state, bits, vec);
}

template <typename Maker>
void run_random_read_benchmark(benchmark::State& state, Maker&& make_vec) {
  using element_type = typename std::decay_t<Maker>::element_type;
  auto const bits = static_cast<std::size_t>(state.range(0));
  auto const n = static_cast<std::size_t>(state.range(1));
  auto const values = make_values<element_type>(n, bits);
  auto const indices = make_indices(n);
  auto vec = make_vec(bits, values);

  for (auto _ : state) {
    auto sum = sum_random(vec, indices);
    benchmark::DoNotOptimize(sum);
  }

  set_common_counters(state, bits, vec);
}

template <typename Maker>
void run_overwrite_benchmark(benchmark::State& state, Maker&& make_vec) {
  using element_type = typename std::decay_t<Maker>::element_type;
  auto const bits = static_cast<std::size_t>(state.range(0));
  auto const n = static_cast<std::size_t>(state.range(1));
  auto const initial_values =
      make_values<element_type>(n, bits, 0x1111111111111111ULL);
  auto const update_values =
      make_values<element_type>(n, bits, 0x2222222222222222ULL);
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
  using element_type = typename std::decay_t<Maker>::element_type;
  auto const bits = static_cast<std::size_t>(state.range(0));
  auto const n = static_cast<std::size_t>(state.range(1));
  auto const values = make_values<element_type>(n, bits);
  auto vec = make_vec(bits, values);

  for (auto _ : state) {
    std::ranges::sort(vec);
    benchmark::ClobberMemory();
  }

  benchmark::DoNotOptimize(vec);

  set_common_counters(state, bits, vec);
}

// unpack(): same volume of reads as sum_sequential, but through the
// hoisted-geometry path (one geometry per call vs. one per element);
// the gap between the two directly quantifies the hoisting benefit
template <typename Maker>
void run_unpack_benchmark(benchmark::State& state, Maker&& make_vec) {
  using element_type = typename std::decay_t<Maker>::element_type;
  auto const bits = static_cast<std::size_t>(state.range(0));
  auto const n = static_cast<std::size_t>(state.range(1));
  auto const values = make_values<element_type>(n, bits);
  auto vec = make_vec(bits, values);

  for (auto _ : state) {
    auto out = vec.unpack();
    benchmark::DoNotOptimize(out);
  }

  set_common_counters(state, bits, vec);
}

// resize(n, value): dominated by fill_field()
template <typename Maker>
void run_resize_fill_benchmark(benchmark::State& state, Maker&& make_vec) {
  using element_type = typename std::decay_t<Maker>::element_type;
  auto const bits = static_cast<std::size_t>(state.range(0));
  auto const n = static_cast<std::size_t>(state.range(1));

  uint64_t seed = 0x0f0f0f0f0f0f0f0fULL;
  auto const fill = make_random_value<element_type>(seed, bits);
  std::vector<element_type> const empty;

  auto sample = make_vec(bits, empty);
  sample.resize(n, fill);

  for (auto _ : state) {
    auto vec = make_vec(bits, empty);
    vec.resize(n, fill);
    benchmark::DoNotOptimize(vec);
    benchmark::ClobberMemory();
  }

  set_common_counters(state, bits, sample);
}

// steady-state front insertion: each iteration shifts n elements through
// copy_encoded_fields(); the pop_back keeps size and capacity constant
template <typename Maker>
void run_insert_front_benchmark(benchmark::State& state, Maker&& make_vec) {
  using element_type = typename std::decay_t<Maker>::element_type;
  auto const bits = static_cast<std::size_t>(state.range(0));
  auto const n = static_cast<std::size_t>(state.range(1));
  auto vec = make_vec(bits, make_values<element_type>(n, bits));

  uint64_t seed = 0x5a5a5a5a5a5a5a5aULL;
  auto const v = make_random_value<element_type>(seed, bits);

  for (auto _ : state) {
    vec.insert(vec.begin(), v);
    vec.pop_back();
  }

  benchmark::DoNotOptimize(vec);

  state.counters["bits"] = static_cast<double>(bits);
  state.counters["values"] = static_cast<double>(n);
  state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(n));
}

// required_widths(): full per-field scan of the vector (the loop behind
// optimize_storage())
template <typename Maker>
void run_required_widths_benchmark(benchmark::State& state, Maker&& make_vec) {
  using element_type = typename std::decay_t<Maker>::element_type;
  auto const bits = static_cast<std::size_t>(state.range(0));
  auto const n = static_cast<std::size_t>(state.range(1));
  auto vec = make_vec(bits, make_values<element_type>(n, bits));

  for (auto _ : state) {
    auto widths = vec.required_widths();
    benchmark::DoNotOptimize(widths);
  }

  set_common_counters(state, bits, vec);
}

// ---------------------------------------------------------------------
// build / push_back
// ---------------------------------------------------------------------

static void bm_build_std_vec(benchmark::State& state) {
  run_build_benchmark(state, std_vec_maker<>{});
}

static void bm_build_compact_packed_vec(benchmark::State& state) {
  run_build_benchmark(state, push_back_maker<compact_packed_vec>{});
}

static void bm_build_compact_auto_packed_vec_exact(benchmark::State& state) {
  run_build_benchmark(state, push_back_maker<compact_auto_packed_vec>{});
}

static void bm_build_compact_auto_packed_vec_growing(benchmark::State& state) {
  run_build_benchmark(state, growing_maker<compact_auto_packed_vec>{});
}

static void bm_build_packed_vec(benchmark::State& state) {
  run_build_benchmark(state, push_back_maker<packed_vec>{});
}

static void bm_build_auto_packed_vec_exact(benchmark::State& state) {
  run_build_benchmark(state, push_back_maker<auto_packed_vec>{});
}

static void bm_build_auto_packed_vec_growing(benchmark::State& state) {
  run_build_benchmark(state, growing_maker<auto_packed_vec>{});
}

static void bm_build_seg_packed_vec_growing(benchmark::State& state) {
  run_build_benchmark(state, seg_maker{});
}

static void bm_build_bulk_compact_packed_vec(benchmark::State& state) {
  run_build_benchmark(state, bulk_insert_maker<compact_packed_vec>{});
}

static void bm_build_bulk_packed_vec(benchmark::State& state) {
  run_build_benchmark(state, bulk_insert_maker<packed_vec>{});
}

static void bm_build_std_tup16_vec(benchmark::State& state) {
  run_build_benchmark(state, std_vec_maker<tup16>{});
}

static void bm_build_compact_tup2_vec(benchmark::State& state) {
  run_build_benchmark(state, push_back_maker<compact_tup2_vec>{});
}

static void bm_build_compact_tup16_vec(benchmark::State& state) {
  run_build_benchmark(state, push_back_maker<compact_tup16_vec>{});
}

static void bm_build_compact_tupmix_vec(benchmark::State& state) {
  run_build_benchmark(state, push_back_maker<compact_tupmix_vec>{});
}

static void bm_build_bulk_compact_tup16_vec(benchmark::State& state) {
  run_build_benchmark(state, bulk_insert_maker<compact_tup16_vec>{});
}

// ---------------------------------------------------------------------
// sequential read
// ---------------------------------------------------------------------

static void bm_sum_sequential_std_vec(benchmark::State& state) {
  run_sequential_read_benchmark(state, std_vec_maker<>{});
}

static void bm_sum_sequential_compact_packed_vec(benchmark::State& state) {
  run_sequential_read_benchmark(state, push_back_maker<compact_packed_vec>{});
}

static void bm_sum_sequential_compact_auto_packed_vec(benchmark::State& state) {
  run_sequential_read_benchmark(state,
                                push_back_maker<compact_auto_packed_vec>{});
}

static void bm_sum_sequential_packed_vec(benchmark::State& state) {
  run_sequential_read_benchmark(state, push_back_maker<packed_vec>{});
}

static void bm_sum_sequential_auto_packed_vec(benchmark::State& state) {
  run_sequential_read_benchmark(state, push_back_maker<auto_packed_vec>{});
}

static void bm_sum_sequential_seg_packed_vec(benchmark::State& state) {
  run_sequential_read_benchmark(state, seg_maker{});
}

static void bm_sum_sequential_std_tup16_vec(benchmark::State& state) {
  run_sequential_read_benchmark(state, std_vec_maker<tup16>{});
}

static void bm_sum_sequential_compact_tup2_vec(benchmark::State& state) {
  run_sequential_read_benchmark(state, push_back_maker<compact_tup2_vec>{});
}

static void bm_sum_sequential_compact_tup16_vec(benchmark::State& state) {
  run_sequential_read_benchmark(state, push_back_maker<compact_tup16_vec>{});
}

static void bm_sum_sequential_compact_tupmix_vec(benchmark::State& state) {
  run_sequential_read_benchmark(state, push_back_maker<compact_tupmix_vec>{});
}

static void bm_sum_sequential_packed_tup16_vec(benchmark::State& state) {
  run_sequential_read_benchmark(state, push_back_maker<packed_tup16_vec>{});
}

// ---------------------------------------------------------------------
// random read
// ---------------------------------------------------------------------

static void bm_sum_random_std_vec(benchmark::State& state) {
  run_random_read_benchmark(state, std_vec_maker<>{});
}

static void bm_sum_random_compact_packed_vec(benchmark::State& state) {
  run_random_read_benchmark(state, push_back_maker<compact_packed_vec>{});
}

static void bm_sum_random_compact_auto_packed_vec(benchmark::State& state) {
  run_random_read_benchmark(state, push_back_maker<compact_auto_packed_vec>{});
}

static void bm_sum_random_packed_vec(benchmark::State& state) {
  run_random_read_benchmark(state, push_back_maker<packed_vec>{});
}

static void bm_sum_random_auto_packed_vec(benchmark::State& state) {
  run_random_read_benchmark(state, push_back_maker<auto_packed_vec>{});
}

static void bm_sum_random_seg_packed_vec(benchmark::State& state) {
  run_random_read_benchmark(state, seg_maker{});
}

static void bm_sum_random_std_tup16_vec(benchmark::State& state) {
  run_random_read_benchmark(state, std_vec_maker<tup16>{});
}

static void bm_sum_random_compact_tup2_vec(benchmark::State& state) {
  run_random_read_benchmark(state, push_back_maker<compact_tup2_vec>{});
}

static void bm_sum_random_compact_tup16_vec(benchmark::State& state) {
  run_random_read_benchmark(state, push_back_maker<compact_tup16_vec>{});
}

static void bm_sum_random_compact_tupmix_vec(benchmark::State& state) {
  run_random_read_benchmark(state, push_back_maker<compact_tupmix_vec>{});
}

static void bm_sum_random_packed_tup16_vec(benchmark::State& state) {
  run_random_read_benchmark(state, push_back_maker<packed_tup16_vec>{});
}

// ---------------------------------------------------------------------
// overwrite existing elements
// ---------------------------------------------------------------------

static void bm_overwrite_std_vec(benchmark::State& state) {
  run_overwrite_benchmark(state, std_vec_maker<>{});
}

static void bm_overwrite_compact_packed_vec(benchmark::State& state) {
  run_overwrite_benchmark(state, push_back_maker<compact_packed_vec>{});
}

static void bm_overwrite_compact_auto_packed_vec(benchmark::State& state) {
  run_overwrite_benchmark(state, push_back_maker<compact_auto_packed_vec>{});
}

static void bm_overwrite_packed_vec(benchmark::State& state) {
  run_overwrite_benchmark(state, push_back_maker<packed_vec>{});
}

static void bm_overwrite_auto_packed_vec(benchmark::State& state) {
  run_overwrite_benchmark(state, push_back_maker<auto_packed_vec>{});
}

static void bm_overwrite_seg_packed_vec(benchmark::State& state) {
  run_overwrite_benchmark(state, seg_maker{});
}

static void bm_overwrite_std_tup16_vec(benchmark::State& state) {
  run_overwrite_benchmark(state, std_vec_maker<tup16>{});
}

static void bm_overwrite_compact_tup2_vec(benchmark::State& state) {
  run_overwrite_benchmark(state, push_back_maker<compact_tup2_vec>{});
}

static void bm_overwrite_compact_tup16_vec(benchmark::State& state) {
  run_overwrite_benchmark(state, push_back_maker<compact_tup16_vec>{});
}

static void bm_overwrite_compact_tupmix_vec(benchmark::State& state) {
  run_overwrite_benchmark(state, push_back_maker<compact_tupmix_vec>{});
}

static void bm_overwrite_packed_tup16_vec(benchmark::State& state) {
  run_overwrite_benchmark(state, push_back_maker<packed_tup16_vec>{});
}

// ---------------------------------------------------------------------
// sort (scalar only: tuple sort convolves comparison cost with access
// cost, which makes before/after deltas hard to attribute)
// ---------------------------------------------------------------------

static void bm_sort_std_vec(benchmark::State& state) {
  run_sort_benchmark(state, std_vec_maker<>{});
}

static void bm_sort_compact_packed_vec(benchmark::State& state) {
  run_sort_benchmark(state, push_back_maker<compact_packed_vec>{});
}

static void bm_sort_compact_auto_packed_vec(benchmark::State& state) {
  run_sort_benchmark(state, push_back_maker<compact_auto_packed_vec>{});
}

static void bm_sort_packed_vec(benchmark::State& state) {
  run_sort_benchmark(state, push_back_maker<packed_vec>{});
}

static void bm_sort_auto_packed_vec(benchmark::State& state) {
  run_sort_benchmark(state, push_back_maker<auto_packed_vec>{});
}

static void bm_sort_seg_packed_vec(benchmark::State& state) {
  run_sort_benchmark(state, seg_maker{});
}

// ---------------------------------------------------------------------
// unpack
// ---------------------------------------------------------------------

static void bm_unpack_compact_packed_vec(benchmark::State& state) {
  run_unpack_benchmark(state, push_back_maker<compact_packed_vec>{});
}

static void bm_unpack_packed_vec(benchmark::State& state) {
  run_unpack_benchmark(state, push_back_maker<packed_vec>{});
}

static void bm_unpack_compact_tup2_vec(benchmark::State& state) {
  run_unpack_benchmark(state, push_back_maker<compact_tup2_vec>{});
}

static void bm_unpack_compact_tup16_vec(benchmark::State& state) {
  run_unpack_benchmark(state, push_back_maker<compact_tup16_vec>{});
}

static void bm_unpack_compact_tupmix_vec(benchmark::State& state) {
  run_unpack_benchmark(state, push_back_maker<compact_tupmix_vec>{});
}

static void bm_unpack_packed_tup16_vec(benchmark::State& state) {
  run_unpack_benchmark(state, push_back_maker<packed_tup16_vec>{});
}

// ---------------------------------------------------------------------
// resize + fill
// ---------------------------------------------------------------------

static void bm_resize_fill_std_vec(benchmark::State& state) {
  run_resize_fill_benchmark(state, std_vec_maker<>{});
}

static void bm_resize_fill_compact_packed_vec(benchmark::State& state) {
  run_resize_fill_benchmark(state, push_back_maker<compact_packed_vec>{});
}

static void bm_resize_fill_packed_vec(benchmark::State& state) {
  run_resize_fill_benchmark(state, push_back_maker<packed_vec>{});
}

static void bm_resize_fill_compact_tup16_vec(benchmark::State& state) {
  run_resize_fill_benchmark(state, push_back_maker<compact_tup16_vec>{});
}

// ---------------------------------------------------------------------
// steady-state front insertion (element shifting)
// ---------------------------------------------------------------------

static void bm_insert_front_std_vec(benchmark::State& state) {
  run_insert_front_benchmark(state, std_vec_maker<>{});
}

static void bm_insert_front_compact_packed_vec(benchmark::State& state) {
  run_insert_front_benchmark(state, push_back_maker<compact_packed_vec>{});
}

static void bm_insert_front_packed_vec(benchmark::State& state) {
  run_insert_front_benchmark(state, push_back_maker<packed_vec>{});
}

static void bm_insert_front_compact_tup16_vec(benchmark::State& state) {
  run_insert_front_benchmark(state, push_back_maker<compact_tup16_vec>{});
}

// ---------------------------------------------------------------------
// required_widths (scan behind optimize_storage)
// ---------------------------------------------------------------------

static void bm_required_widths_compact_packed_vec(benchmark::State& state) {
  run_required_widths_benchmark(state, push_back_maker<compact_packed_vec>{});
}

static void bm_required_widths_packed_vec(benchmark::State& state) {
  run_required_widths_benchmark(state, push_back_maker<packed_vec>{});
}

static void bm_required_widths_compact_tup16_vec(benchmark::State& state) {
  run_required_widths_benchmark(state, push_back_maker<compact_tup16_vec>{});
}

// ---------------------------------------------------------------------
// worst-case widening (scalar)
// ---------------------------------------------------------------------

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
  run_worst_case_build_benchmark(state,
                                 growing_maker<compact_auto_packed_vec>{});
}

static void
bm_build_compact_auto_packed_vec_worst_case_exact(benchmark::State& state) {
  run_worst_case_build_benchmark(state,
                                 push_back_maker<compact_auto_packed_vec>{});
}

static void
bm_build_compact_packed_vec_worst_case_reference(benchmark::State& state) {
  run_worst_case_build_benchmark(state, push_back_maker<compact_packed_vec>{});
}

static void
bm_build_auto_packed_vec_worst_case_widening(benchmark::State& state) {
  run_worst_case_build_benchmark(state, growing_maker<auto_packed_vec>{});
}

static void bm_build_auto_packed_vec_worst_case_exact(benchmark::State& state) {
  run_worst_case_build_benchmark(state, push_back_maker<auto_packed_vec>{});
}

static void bm_build_packed_vec_worst_case_reference(benchmark::State& state) {
  run_worst_case_build_benchmark(state, push_back_maker<packed_vec>{});
}

static void bm_build_std_vec_worst_case_reference(benchmark::State& state) {
  run_worst_case_build_benchmark(state, std_vec_maker<>{});
}

} // namespace

// scalar args: {bits, n}; tuple args: {per-field bits, n}

#define SCALAR_ARGS ArgsProduct({{5, 13, 17, 31}, {4096, 65536}})
#define TUPLE_ARGS ArgsProduct({{3, 7, 12}, {4096, 65536}})

// build / push_back

BENCHMARK(bm_build_std_vec)->SCALAR_ARGS;
BENCHMARK(bm_build_compact_packed_vec)->SCALAR_ARGS;
BENCHMARK(bm_build_compact_auto_packed_vec_exact)->SCALAR_ARGS;
BENCHMARK(bm_build_compact_auto_packed_vec_growing)->SCALAR_ARGS;
BENCHMARK(bm_build_packed_vec)->SCALAR_ARGS;
BENCHMARK(bm_build_auto_packed_vec_exact)->SCALAR_ARGS;
BENCHMARK(bm_build_auto_packed_vec_growing)->SCALAR_ARGS;
BENCHMARK(bm_build_seg_packed_vec_growing)->SCALAR_ARGS;
BENCHMARK(bm_build_bulk_compact_packed_vec)->SCALAR_ARGS;
BENCHMARK(bm_build_bulk_packed_vec)->SCALAR_ARGS;

BENCHMARK(bm_build_std_tup16_vec)->TUPLE_ARGS;
BENCHMARK(bm_build_compact_tup2_vec)->TUPLE_ARGS;
BENCHMARK(bm_build_compact_tup16_vec)->TUPLE_ARGS;
BENCHMARK(bm_build_compact_tupmix_vec)->TUPLE_ARGS;
BENCHMARK(bm_build_bulk_compact_tup16_vec)->TUPLE_ARGS;

// sequential read

BENCHMARK(bm_sum_sequential_std_vec)->SCALAR_ARGS;
BENCHMARK(bm_sum_sequential_compact_packed_vec)->SCALAR_ARGS;
BENCHMARK(bm_sum_sequential_compact_auto_packed_vec)->SCALAR_ARGS;
BENCHMARK(bm_sum_sequential_packed_vec)->SCALAR_ARGS;
BENCHMARK(bm_sum_sequential_auto_packed_vec)->SCALAR_ARGS;
BENCHMARK(bm_sum_sequential_seg_packed_vec)->SCALAR_ARGS;

BENCHMARK(bm_sum_sequential_std_tup16_vec)->TUPLE_ARGS;
BENCHMARK(bm_sum_sequential_compact_tup2_vec)->TUPLE_ARGS;
BENCHMARK(bm_sum_sequential_compact_tup16_vec)->TUPLE_ARGS;
BENCHMARK(bm_sum_sequential_compact_tupmix_vec)->TUPLE_ARGS;
BENCHMARK(bm_sum_sequential_packed_tup16_vec)->TUPLE_ARGS;

// random read

BENCHMARK(bm_sum_random_std_vec)->SCALAR_ARGS;
BENCHMARK(bm_sum_random_compact_packed_vec)->SCALAR_ARGS;
BENCHMARK(bm_sum_random_compact_auto_packed_vec)->SCALAR_ARGS;
BENCHMARK(bm_sum_random_packed_vec)->SCALAR_ARGS;
BENCHMARK(bm_sum_random_auto_packed_vec)->SCALAR_ARGS;
BENCHMARK(bm_sum_random_seg_packed_vec)->SCALAR_ARGS;

BENCHMARK(bm_sum_random_std_tup16_vec)->TUPLE_ARGS;
BENCHMARK(bm_sum_random_compact_tup2_vec)->TUPLE_ARGS;
BENCHMARK(bm_sum_random_compact_tup16_vec)->TUPLE_ARGS;
BENCHMARK(bm_sum_random_compact_tupmix_vec)->TUPLE_ARGS;
BENCHMARK(bm_sum_random_packed_tup16_vec)->TUPLE_ARGS;

// overwrite

BENCHMARK(bm_overwrite_std_vec)->SCALAR_ARGS;
BENCHMARK(bm_overwrite_compact_packed_vec)->SCALAR_ARGS;
BENCHMARK(bm_overwrite_compact_auto_packed_vec)->SCALAR_ARGS;
BENCHMARK(bm_overwrite_packed_vec)->SCALAR_ARGS;
BENCHMARK(bm_overwrite_auto_packed_vec)->SCALAR_ARGS;
BENCHMARK(bm_overwrite_seg_packed_vec)->SCALAR_ARGS;

BENCHMARK(bm_overwrite_std_tup16_vec)->TUPLE_ARGS;
BENCHMARK(bm_overwrite_compact_tup2_vec)->TUPLE_ARGS;
BENCHMARK(bm_overwrite_compact_tup16_vec)->TUPLE_ARGS;
BENCHMARK(bm_overwrite_compact_tupmix_vec)->TUPLE_ARGS;
BENCHMARK(bm_overwrite_packed_tup16_vec)->TUPLE_ARGS;

// sort

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

// unpack

BENCHMARK(bm_unpack_compact_packed_vec)->SCALAR_ARGS;
BENCHMARK(bm_unpack_packed_vec)->SCALAR_ARGS;
BENCHMARK(bm_unpack_compact_tup2_vec)->TUPLE_ARGS;
BENCHMARK(bm_unpack_compact_tup16_vec)->TUPLE_ARGS;
BENCHMARK(bm_unpack_compact_tupmix_vec)->TUPLE_ARGS;
BENCHMARK(bm_unpack_packed_tup16_vec)->TUPLE_ARGS;

// resize + fill

BENCHMARK(bm_resize_fill_std_vec)->SCALAR_ARGS;
BENCHMARK(bm_resize_fill_compact_packed_vec)->SCALAR_ARGS;
BENCHMARK(bm_resize_fill_packed_vec)->SCALAR_ARGS;
BENCHMARK(bm_resize_fill_compact_tup16_vec)->TUPLE_ARGS;

// steady-state front insertion (each iteration is O(n), keep n modest)

BENCHMARK(bm_insert_front_std_vec)->ArgsProduct({{5, 13, 17, 31}, {4096}});
BENCHMARK(bm_insert_front_compact_packed_vec)
    ->ArgsProduct({{5, 13, 17, 31}, {4096}});
BENCHMARK(bm_insert_front_packed_vec)->ArgsProduct({{5, 13, 17, 31}, {4096}});
BENCHMARK(bm_insert_front_compact_tup16_vec)->ArgsProduct({{3, 7, 12}, {4096}});

// required_widths

BENCHMARK(bm_required_widths_compact_packed_vec)->SCALAR_ARGS;
BENCHMARK(bm_required_widths_packed_vec)->SCALAR_ARGS;
BENCHMARK(bm_required_widths_compact_tup16_vec)->TUPLE_ARGS;

// worst-case widening

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
