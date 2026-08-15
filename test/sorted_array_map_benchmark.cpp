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

#include <array>
#include <concepts>
#include <numeric>
#include <random>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <benchmark/benchmark.h>

#include <dwarfs/container/sorted_array_map.h>

namespace {

using namespace dwarfs::container;

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
  std::ranges::generate(v, [&] { return dist(gen); });
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

constexpr inline std::size_t KeyDigits = 6;

template <std::size_t N, std::size_t Len>
struct key_storage {
  static_assert(Len > KeyDigits);

  std::array<char, N * Len> chars{};

  constexpr key_storage() {
    for (std::size_t i = 0; i < N; ++i) {
      auto* p = chars.data() + i * Len;
      auto v = i;

      for (std::size_t j = KeyDigits; j-- > 0;) {
        p[j] = static_cast<char>('0' + v % 10);
        v /= 10;
      }

      for (std::size_t j = KeyDigits; j + 1 < Len; ++j) {
        p[j] = 'x';
      }

      p[Len - 1] = '\0';
    }
  }
};

template <std::size_t N, std::size_t Len>
constexpr inline key_storage<N, Len> keys{};

template <std::size_t N, std::size_t Len>
constexpr char const* key_cstr(std::size_t i) {
  return keys<N, Len>.chars.data() + i * Len;
}

template <std::size_t N, std::size_t Len>
constexpr std::string_view key_view(std::size_t i) {
  return std::string_view{key_cstr<N, Len>(i), Len - 1};
}

template <std::size_t N, std::size_t Len>
constexpr auto make_view_pairs() {
  std::array<std::pair<std::string_view, int>, N> pairs{};
  for (std::size_t i = 0; i < N; ++i) {
    pairs[i] = {key_view<N, Len>(i), static_cast<int>(i)};
  }
  return pairs;
}

template <std::size_t N, std::size_t Len>
constexpr auto make_cstr_pairs() {
  std::array<std::pair<char const*, int>, N> pairs{};
  for (std::size_t i = 0; i < N; ++i) {
    pairs[i] = {key_cstr<N, Len>(i), static_cast<int>(i)};
  }
  return pairs;
}

template <std::size_t N, std::size_t Len>
auto make_string_map() {
  std::array<std::pair<std::string, int>, N> pairs;
  for (std::size_t i = 0; i < N; ++i) {
    pairs[i] = {std::string{key_view<N, Len>(i)}, static_cast<int>(i)};
  }
  return sorted_array_map<std::string, int, N>{std::move(pairs)};
}

template <std::size_t N, std::size_t Len>
constexpr auto view_map = sorted_array_map{make_view_pairs<N, Len>()};

struct transparent_string_hash {
  using is_transparent = void;

  std::size_t operator()(char const* s) const {
    return std::hash<std::string_view>{}(s);
  }
  std::size_t operator()(std::string_view s) const {
    return std::hash<std::string_view>{}(s);
  }
  std::size_t operator()(std::string const& s) const {
    return std::hash<std::string_view>{}(s);
  }
};

using transparent_umap =
    std::unordered_map<std::string, int, transparent_string_hash,
                       std::equal_to<>>;

template <std::size_t N, std::size_t Len>
auto make_unordered_map() {
  transparent_umap map;
  map.reserve(N);
  for (std::size_t i = 0; i < N; ++i) {
    map.emplace(std::string{key_view<N, Len>(i)}, static_cast<int>(i));
  }
  return map;
}

template <typename Arg, std::size_t N, std::size_t Len>
std::vector<Arg> make_args(std::vector<int> const& indices) {
  std::vector<Arg> args;
  args.reserve(indices.size());

  for (int i : indices) {
    if constexpr (std::same_as<Arg, char const*>) {
      args.push_back(key_cstr<N, Len>(static_cast<std::size_t>(i)));
    } else {
      args.emplace_back(key_view<N, Len>(static_cast<std::size_t>(i)));
    }
  }

  return args;
}

template <typename Map, typename Arg>
void run_lookups(::benchmark::State& state, Map const& map,
                 std::vector<Arg> const& args) {
  for (auto _ : state) {
    for (auto const& k : args) {
      auto v = map.at(k);
      benchmark::DoNotOptimize(v);
    }
  }
  state.SetItemsProcessed(state.iterations() * args.size());
}

template <typename Arg>
void run_lookups(::benchmark::State& state, transparent_umap const& map,
                 std::vector<Arg> const& args) {
  for (auto _ : state) {
    for (auto const& k : args) {
      auto v = map.find(k)->second;
      benchmark::DoNotOptimize(v);
    }
  }
  state.SetItemsProcessed(state.iterations() * args.size());
}

template <std::size_t N, std::size_t Len, typename Arg>
void lookup_view_key_constexpr(::benchmark::State& state) {
  auto const args =
      make_args<Arg, N, Len>(random_vector(0, N - 1, LookupCount));
  run_lookups(state, view_map<N, Len>, args);
}

template <std::size_t N, std::size_t Len, typename Arg>
void lookup_view_key_runtime(::benchmark::State& state) {
  auto const args =
      make_args<Arg, N, Len>(random_vector(0, N - 1, LookupCount));
  sorted_array_map<std::string_view, int, N> map{make_view_pairs<N, Len>()};
  run_lookups(state, map, args);
}

template <std::size_t N, std::size_t Len, typename Arg>
void lookup_string_key(::benchmark::State& state) {
  auto const args =
      make_args<Arg, N, Len>(random_vector(0, N - 1, LookupCount));
  auto const map = make_string_map<N, Len>();
  run_lookups(state, map, args);
}

template <std::size_t N, std::size_t Len, typename Arg>
void lookup_unordered_string(::benchmark::State& state) {
  auto const args =
      make_args<Arg, N, Len>(random_vector(0, N - 1, LookupCount));
  auto const map = make_unordered_map<N, Len>();
  run_lookups(state, map, args);
}

} // namespace

#define SAM_BENCH_ARGS(fn, N, Len)                                             \
  BENCHMARK_TEMPLATE(fn, N, Len, char const*);                                 \
  BENCHMARK_TEMPLATE(fn, N, Len, std::string_view);                            \
  BENCHMARK_TEMPLATE(fn, N, Len, std::string)

#define SAM_BENCH_MATRIX(N, Len)                                               \
  SAM_BENCH_ARGS(lookup_view_key_constexpr, N, Len);                           \
  SAM_BENCH_ARGS(lookup_view_key_runtime, N, Len);                             \
  SAM_BENCH_ARGS(lookup_string_key, N, Len);                                   \
  SAM_BENCH_ARGS(lookup_unordered_string, N, Len);

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

SAM_BENCH_MATRIX(8, 16);
SAM_BENCH_MATRIX(32, 16);
SAM_BENCH_MATRIX(64, 16);
SAM_BENCH_MATRIX(256, 16);
SAM_BENCH_MATRIX(1024, 16);

SAM_BENCH_MATRIX(256, 8);
SAM_BENCH_MATRIX(256, 32);
SAM_BENCH_MATRIX(256, 64);
SAM_BENCH_MATRIX(256, 128);

BENCHMARK_MAIN();
