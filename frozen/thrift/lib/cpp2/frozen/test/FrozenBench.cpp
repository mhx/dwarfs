/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wfloat-equal"
#endif

#include <cstdint>
#include <cstring>
#include <map>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <benchmark/benchmark.h>

#include <dwarfs/conv.h>

#include <thrift/lib/cpp2/frozen/FrozenUtil.h>
#include <thrift/lib/cpp2/frozen/HintTypes.h>

namespace apache::thrift::frozen {
namespace {

constexpr int kRows = 490;
constexpr int kCols = 51;

uint32_t rand32(uint32_t max) {
  static thread_local std::mt19937 rng{42};
  std::uniform_int_distribution<uint32_t> dist(0, max - 1);
  return dist(rng);
}

template <class T, class Inner = std::vector<T>>
std::vector<Inner> make_matrix() {
  std::vector<Inner> values;
  values.reserve(kRows);

  for (int y = 0; y < kRows; ++y) {
    auto& row = values.emplace_back();
    row.reserve(kCols);
    for (int x = 0; x < kCols; ++x) {
      row.push_back(rand32(12000) - 6000);
    }
  }

  return values;
}

template <class Matrix>
using matrix_element_t = std::remove_cvref_t<
    decltype(std::declval<Matrix const&>()[std::size_t{}][std::size_t{}])>;

template <class Matrix>
using matrix_sum_t = std::conditional_t<
    std::is_floating_point_v<matrix_element_t<Matrix>>,
    double,
    std::int64_t>;

constexpr std::int64_t k_sum_items_per_iteration =
    static_cast<std::int64_t>(kRows) * kCols;

constexpr std::int64_t k_sum_cols_items_per_iteration =
    static_cast<std::int64_t>(kCols) * kRows -
    static_cast<std::int64_t>(kCols) * (kCols - 1) / 2;

template <class Matrix>
void benchmark_sum(benchmark::State& state, Matrix const& matrix) {
  using sum_type = matrix_sum_t<Matrix>;
  sum_type sum = 0;

  for (auto _ : state) {
    for (auto const& row : matrix) {
      for (auto const& value : row) {
        sum += static_cast<sum_type>(value);
      }
    }
  }

  benchmark::DoNotOptimize(sum);
  state.SetItemsProcessed(state.iterations() * k_sum_items_per_iteration);
}

template <class Matrix>
void benchmark_sum_cols(benchmark::State& state, Matrix const& matrix) {
  using sum_type = matrix_sum_t<Matrix>;
  sum_type sum = 0;

  for (auto _ : state) {
    for (std::size_t x = 0; x < kCols; ++x) {
      for (std::size_t y = x; y < kRows; ++y) {
        sum += static_cast<sum_type>(matrix[y][x]);
      }
    }
  }

  benchmark::DoNotOptimize(sum);
  state.SetItemsProcessed(state.iterations() * k_sum_cols_items_per_iteration);
}

template <class Matrix>
void benchmark_sum_saved_cols(benchmark::State& state, Matrix const& matrix) {
  using sum_type = matrix_sum_t<Matrix>;

  std::vector<typename Matrix::value_type> rows;
  rows.reserve(kRows);
  for (std::size_t y = 0; y < kRows; ++y) {
    rows.push_back(matrix[y]);
  }

  sum_type sum = 0;
  for (auto _ : state) {
    for (std::size_t x = 0; x < kCols; ++x) {
      for (std::size_t y = x; y < kRows; ++y) {
        sum += static_cast<sum_type>(rows[y][x]);
      }
    }
  }

  benchmark::DoNotOptimize(sum);
  state.SetItemsProcessed(state.iterations() * k_sum_cols_items_per_iteration);
}

const auto vvi16 = make_matrix<std::int16_t>();
const auto vvi32 = make_matrix<std::int32_t>();
const auto vvi64 = make_matrix<std::int64_t>();
const auto fvvi16 = freeze(vvi16);
const auto fvvi32 = freeze(vvi32);
const auto fvvi64 = freeze(vvi64);
const auto fuvvi16 =
    freeze(make_matrix<std::int16_t, VectorUnpacked<std::int16_t>>());
const auto fuvvi32 =
    freeze(make_matrix<std::int32_t, VectorUnpacked<std::int32_t>>());
const auto fuvvi64 =
    freeze(make_matrix<std::int64_t, VectorUnpacked<std::int64_t>>());
const auto vvf32 = make_matrix<float>();
const auto fvvf32 = freeze(vvf32);

constexpr std::size_t k_entries = 1'000'000;
constexpr std::size_t k_chunk_size = 1'000;

template <typename T>
FixedSizeString<sizeof(T)> copy_to_fixed_size_str(T value) {
  static_assert(std::is_trivially_copyable_v<T>);

  FixedSizeString<sizeof(T)> value_str;
  value_str.resize(sizeof(T));
  std::memcpy(
      value_str.data(), reinterpret_cast<void const*>(&value), sizeof(T));
  return value_str;
}

template <typename K>
K make_key() {
  return dwarfs::to<K>(rand32(k_entries));
}

template <>
FixedSizeString<8> make_key<FixedSizeString<8>>() {
  return copy_to_fixed_size_str(dwarfs::to<std::int64_t>(rand32(k_entries)));
}

template <typename K>
std::vector<K> make_keys() {
  std::vector<K> keys(k_chunk_size);
  for (auto& key : keys) {
    key = make_key<K>();
  }
  return keys;
}

template <class Map>
Map make_map() {
  Map hist;
  using key_type = typename Map::key_type;

  for (std::size_t i = 0; i < k_entries; ++i) {
    ++hist[make_key<key_type>()];
  }

  return hist;
}

template <
    typename K,
    typename V,
    template <typename, typename> class ContainerType>
auto convert_to_fixed_size_map(ContainerType<K, V> const& map) {
  ContainerType<FixedSizeString<sizeof(K)>, FixedSizeString<sizeof(V)>> str_map;
  if constexpr (requires { str_map.reserve(map.size()); }) {
    str_map.reserve(map.size());
  }

  for (auto const& [key, value] : map) {
    str_map[copy_to_fixed_size_str(key)] = copy_to_fixed_size_str(value);
  }

  return str_map;
}

template <typename K, typename V>
using unordered_map_type = std::unordered_map<K, V>;

template <typename K>
struct owned_key {
  using type = K;
};

template <>
struct owned_key<std::string_view> {
  using type = std::string;
};

template <>
struct owned_key<std::span<std::uint8_t const>> {
  using type = FixedSizeString<8>;
};

using fixed_size_string_view =
    typename detail::FixedSizeStringLayout<FixedSizeString<8>>::View;

template <>
struct owned_key<fixed_size_string_view> {
  using type = FixedSizeString<8>;
};

template <class M, class T>
  requires(!std::same_as<typename M::key_type, fixed_size_string_view>)
auto map_find(M const& map, T const& key) -> typename M::const_iterator {
  return map.find(key);
}

template <class M, class T>
  requires std::same_as<typename M::key_type, fixed_size_string_view> &&
    std::same_as<T, FixedSizeString<8>>
auto map_find(M const& map, T const& key) -> typename M::const_iterator {
  auto key_view = std::span<std::uint8_t const>{
      reinterpret_cast<std::uint8_t const*>(key.data()), key.size()};
  return map.find(key_view);
}

template <class Map>
void benchmark_lookup(benchmark::State& state, Map const& map) {
  using lookup_key_type = typename owned_key<typename Map::key_type>::type;

  // Pre-generate a key chunk outside the timed loop and cycle through it.
  // That keeps setup out of the measurement without needing PauseTiming().
  auto keys = make_keys<lookup_key_type>();

  std::size_t key_index = 0;
  std::size_t hits = 0;

  for (auto _ : state) {
    auto const& key = keys[key_index];
    key_index = (key_index + 1) % keys.size();

    auto found = map_find(map, key);
    if (found != map.end()) {
      ++hits;
    }
  }

  benchmark::DoNotOptimize(hits);
  state.SetItemsProcessed(state.iterations());
}

const auto hash_map_f32 = make_map<std::unordered_map<float, int>>();
const auto hash_map_i32 = make_map<std::unordered_map<std::int32_t, int>>();
const auto hash_map_i64 = make_map<std::unordered_map<std::int64_t, int>>();
const auto hash_map_str = make_map<std::unordered_map<std::string, int>>();
const auto hash_map_fixed_str =
    convert_to_fixed_size_map<std::int64_t, int, unordered_map_type>(
        hash_map_i64);

const auto frozen_hash_map_f32 = freeze(hash_map_f32);
const auto frozen_hash_map_i32 = freeze(hash_map_i32);
const auto frozen_hash_map_i64 = freeze(hash_map_i64);
const auto frozen_hash_map_str = freeze(hash_map_str);
const auto frozen_hash_map_fixed_str = freeze(hash_map_fixed_str);

const auto map_f32 =
    std::map<float, int>(hash_map_f32.begin(), hash_map_f32.end());
const auto map_i32 =
    std::map<std::int32_t, int>(hash_map_i32.begin(), hash_map_i32.end());
const auto map_i64 =
    std::map<std::int64_t, int>(hash_map_i64.begin(), hash_map_i64.end());

const auto frozen_map_f32 = freeze(map_f32);
const auto frozen_map_i32 = freeze(map_i32);
const auto frozen_map_i64 = freeze(map_i64);

template <class T>
void benchmark_old_freeze_data_to_string(
    benchmark::State& state, T const& data) {
  const auto layout = maximumLayout<T>();
  std::size_t total_size = 0;

  for (auto _ : state) {
    std::string out;
    out.resize(frozenSize(data, layout));

    auto write_range = std::span<std::uint8_t>{
        reinterpret_cast<std::uint8_t*>(out.data()), out.size()};

    ByteRangeFreezer::freeze(layout, data, write_range);
    out.resize(out.size() - write_range.size());

    total_size += out.size();
  }

  benchmark::DoNotOptimize(total_size);
  state.SetItemsProcessed(state.iterations());
}

template <class T>
void benchmark_freeze_data_to_string(benchmark::State& state, T const& data) {
  const auto layout = maximumLayout<T>();
  std::size_t total_size = 0;

  for (auto _ : state) {
    total_size += freezeDataToString(data, layout).size();
  }

  benchmark::DoNotOptimize(total_size);
  state.SetItemsProcessed(state.iterations());
}

const auto strings =
    std::vector<std::string>{"one", "two", "three", "four", "five"};
const auto tuple =
    std::make_pair(std::make_pair(1.3, 2.4), std::make_pair(4.5, 'x'));

template <class Fn, class Arg>
void register_case(std::string const& name, Fn fn, Arg const& arg) {
  benchmark::RegisterBenchmark(
      name.c_str(), [fn, &arg](benchmark::State& state) { fn(state, arg); });
}

void register_benchmarks() {
  constexpr auto run_sum = [](benchmark::State& state, auto const& arg) {
    benchmark_sum(state, arg);
  };
  constexpr auto run_sum_cols = [](benchmark::State& state, auto const& arg) {
    benchmark_sum_cols(state, arg);
  };
  constexpr auto run_sum_saved_cols = [](benchmark::State& state,
                                         auto const& arg) {
    benchmark_sum_saved_cols(state, arg);
  };
  constexpr auto run_lookup = [](benchmark::State& state, auto const& arg) {
    benchmark_lookup(state, arg);
  };
  constexpr auto run_freeze_data_to_string = [](benchmark::State& state,
                                                auto const& arg) {
    benchmark_freeze_data_to_string(state, arg);
  };
  constexpr auto run_old_freeze_data_to_string = [](benchmark::State& state,
                                                    auto const& arg) {
    benchmark_old_freeze_data_to_string(state, arg);
  };

  register_case("benchmark_sum/vvi16", run_sum, vvi16);
  register_case("benchmark_sum/fvvi16", run_sum, fvvi16);
  register_case("benchmark_sum/fuvvi16", run_sum, fuvvi16);
  register_case("benchmark_sum/vvi32", run_sum, vvi32);
  register_case("benchmark_sum/fvvi32", run_sum, fvvi32);
  register_case("benchmark_sum/fuvvi32", run_sum, fuvvi32);
  register_case("benchmark_sum/vvi64", run_sum, vvi64);
  register_case("benchmark_sum/fvvi64", run_sum, fvvi64);
  register_case("benchmark_sum/fuvvi64", run_sum, fuvvi64);
  register_case("benchmark_sum/vvf32", run_sum, vvf32);
  register_case("benchmark_sum/fvvf32", run_sum, fvvf32);

  register_case("benchmark_sum_cols/vvi32", run_sum_cols, vvi32);
  register_case("benchmark_sum_cols/fvvi32", run_sum_cols, fvvi32);
  register_case("benchmark_sum_cols/fuvvi32", run_sum_cols, fuvvi32);
  register_case("benchmark_sum_saved_cols/fvvi32", run_sum_saved_cols, fvvi32);
  register_case(
      "benchmark_sum_saved_cols/fuvvi32", run_sum_saved_cols, fuvvi32);

  register_case("benchmark_lookup/hash_map_f32", run_lookup, hash_map_f32);
  register_case(
      "benchmark_lookup/frozen_hash_map_f32", run_lookup, frozen_hash_map_f32);
  register_case("benchmark_lookup/hash_map_i32", run_lookup, hash_map_i32);
  register_case(
      "benchmark_lookup/frozen_hash_map_i32", run_lookup, frozen_hash_map_i32);
  register_case("benchmark_lookup/hash_map_i64", run_lookup, hash_map_i64);
  register_case(
      "benchmark_lookup/frozen_hash_map_i64", run_lookup, frozen_hash_map_i64);
  register_case("benchmark_lookup/hash_map_str", run_lookup, hash_map_str);
  register_case(
      "benchmark_lookup/frozen_hash_map_str", run_lookup, frozen_hash_map_str);
  register_case(
      "benchmark_lookup/hash_map_fixed_str", run_lookup, hash_map_fixed_str);
  register_case(
      "benchmark_lookup/frozen_hash_map_fixed_str",
      run_lookup,
      frozen_hash_map_fixed_str);

  register_case("benchmark_lookup/map_f32", run_lookup, map_f32);
  register_case("benchmark_lookup/frozen_map_f32", run_lookup, frozen_map_f32);
  register_case("benchmark_lookup/map_i32", run_lookup, map_i32);
  register_case("benchmark_lookup/frozen_map_i32", run_lookup, frozen_map_i32);
  register_case("benchmark_lookup/map_i64", run_lookup, map_i64);
  register_case("benchmark_lookup/frozen_map_i64", run_lookup, frozen_map_i64);

  register_case(
      "benchmark_freeze_data_to_string/vvf32",
      run_freeze_data_to_string,
      vvf32);
  register_case(
      "benchmark_old_freeze_data_to_string/vvf32",
      run_old_freeze_data_to_string,
      vvf32);

  register_case(
      "benchmark_freeze_data_to_string/tuple",
      run_freeze_data_to_string,
      tuple);
  register_case(
      "benchmark_old_freeze_data_to_string/tuple",
      run_old_freeze_data_to_string,
      tuple);

  register_case(
      "benchmark_freeze_data_to_string/strings",
      run_freeze_data_to_string,
      strings);
  register_case(
      "benchmark_old_freeze_data_to_string/strings",
      run_old_freeze_data_to_string,
      strings);

  register_case(
      "benchmark_freeze_data_to_string/hash_map_i32",
      run_freeze_data_to_string,
      hash_map_i32);
  register_case(
      "benchmark_old_freeze_data_to_string/hash_map_i32",
      run_old_freeze_data_to_string,
      hash_map_i32);
}

} // namespace
} // namespace apache::thrift::frozen

int main(int argc, char** argv) {
  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }

  apache::thrift::frozen::register_benchmarks();
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  return 0;
}

#if 0
Old results using folly-benchmark:

clang version 21.1.8, i9-13900K
                                         [folly::Bits]        [dwarfs::bit_view]
================================================================================
FrozenBench.cpp                           time/iter iters/s    time/iter iters/s
================================================================================
benchmarkSum(vvi16)                           2.22us 450.75K      2.23us 447.67K
benchmarkSum(fvvi16)                         49.11us  20.36K     36.24us  27.59K
benchmarkSum(fuvvi16)                         4.00us 250.19K      4.01us 249.56K
benchmarkSum(vvi32)                           2.23us 448.25K      2.22us 451.37K
benchmarkSum(fvvi32)                         40.03us  24.98K     30.07us  33.25K
benchmarkSum(fuvvi32)                         3.32us 301.51K      3.45us 289.90K
benchmarkSum(vvi64)                           3.09us 323.80K      3.24us 308.67K
benchmarkSum(fvvi64)                         47.74us  20.95K     30.95us  32.31K
benchmarkSum(fuvvi64)                         5.34us 187.42K      5.36us 186.56K
benchmarkSum(vvf32)                          93.91us  10.65K     93.92us  10.65K
benchmarkSum(fvvf32)                         76.24us  13.12K     76.24us  13.12K
--------------------------------------------------------------------------------
benchmarkSumCols(vvi32)                      13.55us  73.83K     13.55us  73.80K
benchmarkSumCols(fvvi32)                    108.55us   9.21K     94.20us  10.62K
benchmarkSumCols(fuvvi32)                    25.18us  39.71K     20.25us  49.38K
benchmarkSumSavedCols(fvvi32)                49.93us  20.03K     42.99us  23.26K
benchmarkSumSavedCols(fuvvi32)                5.95us 168.10K      5.95us 168.06K
--------------------------------------------------------------------------------
benchmarkLookup(hashMap_f32)                 48.77ns  20.51M     48.76ns  20.51M
benchmarkLookup(frozenHashMap_f32)           26.15ns  38.24M     25.51ns  39.21M
benchmarkLookup(hashMap_i32)                 14.79ns  67.61M     14.75ns  67.81M
benchmarkLookup(frozenHashMap_i32)           16.73ns  59.76M     16.19ns  61.76M
benchmarkLookup(hashMap_i64)                 15.94ns  62.72M     16.00ns  62.49M
benchmarkLookup(frozenHashMap_i64)           15.67ns  63.82M     15.59ns  64.14M
benchmarkLookup(hashMap_str)                 74.52ns  13.42M     78.00ns  12.82M
benchmarkLookup(frozenHashMap_str)           41.80ns  23.92M     41.91ns  23.86M
benchmarkLookup(hashMap_fixedStr)            30.06ns  33.27M     30.83ns  32.43M
benchmarkLookup(frozenHashMap_fixedStr)      12.90ns  77.49M     11.71ns  85.37M
--------------------------------------------------------------------------------
benchmarkLookup(map_f32)                    173.01ns   5.78M    185.07ns   5.40M
benchmarkLookup(frozenMap_f32)              134.42ns   7.44M    135.89ns   7.36M
benchmarkLookup(map_i32)                    127.30ns   7.86M    127.15ns   7.86M
benchmarkLookup(frozenMap_i32)              171.80ns   5.82M    173.71ns   5.76M
benchmarkLookup(map_i64)                    129.24ns   7.74M    125.88ns   7.94M
benchmarkLookup(frozenMap_i64)              176.49ns   5.67M    175.66ns   5.69M
--------------------------------------------------------------------------------
benchmarkFreezeDataToString(vvf32)           67.52us  14.81K     65.43us  15.28K
benchmarkOldFreezeDataToString(vvf32)       128.04us   7.81K    128.47us   7.78K
benchmarkFreezeDataToString(tuple)          102.29ns   9.78M    104.10ns   9.61M
benchmarkOldFreezeDataToString(tuple)        52.44ns  19.07M     52.52ns  19.04M
benchmarkFreezeDataToString(strings)        425.90ns   2.35M    435.59ns   2.30M
benchmarkOldFreezeDataToString(strings)     163.27ns   6.12M    163.72ns   6.11M
benchmarkFreezeDataToString(hashMap_i32)     29.37ms   34.04     29.62ms   33.76
benchmarkOldFreezeDataToString(hashMap_i32)  50.45ms   19.82     50.92ms   19.64
#endif
