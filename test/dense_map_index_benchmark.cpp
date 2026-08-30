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
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <numeric>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

#include <benchmark/benchmark.h>

#include <parallel_hashmap/phmap.h>

#include <dwarfs/container/chunked_append_only_vector.h>
#include <dwarfs/container/dense_map_index.h>

namespace {

using dwarfs::container::basic_dense_map_index;
using dwarfs::container::chunked_append_only_vector;
using dwarfs::container::default_value_hash;

// Store / index policy matrix

struct vector_store_tag {
  template <typename T>
  using type = std::vector<T>;

  static constexpr std::string_view name = "vector";
};

struct chunked_store_tag {
  template <typename T>
  using type = chunked_append_only_vector<T>;

  static constexpr std::string_view name = "chunked";
};

struct std_unordered_set_tag {
  template <typename Hash, typename Equal>
  using type = std::unordered_set<std::size_t, Hash, Equal>;

  static constexpr std::string_view name = "std_unordered_set";
};

struct phmap_flat_hash_set_tag {
  template <typename Hash, typename Equal>
  using type = phmap::flat_hash_set<std::size_t, Hash, Equal>;

  static constexpr std::string_view name = "phmap_flat_hash_set";
};

struct phmap_parallel_flat_hash_set_tag {
  template <typename Hash, typename Equal>
  using type = phmap::parallel_flat_hash_set<std::size_t, Hash, Equal>;

  static constexpr std::string_view name = "phmap_parallel_flat_hash_set";
};

template <typename T, typename StoreTag, typename IndexTag>
struct benchmark_policy {
  using key_type = typename T::first_type;
  using store_type = typename StoreTag::template type<T>;
  using hash_type = default_value_hash<key_type>;
  using equal_type = std::equal_to<>;

  template <typename Hash, typename Equal>
  using index_type = typename IndexTag::template type<Hash, Equal>;
};

template <typename Key, typename Mapped, typename StoreTag, typename IndexTag>
struct benchmark_config {
  using key_type = Key;
  using mapped_type = Mapped;
  using entry_type = std::pair<Key, Mapped>;
  using store_tag = StoreTag;
  using index_tag = IndexTag;

  template <typename T>
  using policy = benchmark_policy<T, StoreTag, IndexTag>;

  using store_type = typename policy<entry_type>::store_type;
  using index_type = basic_dense_map_index<Key, Mapped, policy>;
};

// Deterministic input generation

[[nodiscard]] constexpr std::uint64_t mix64(std::uint64_t x) noexcept {
  x += 0x9e3779b97f4a7c15ull;
  x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
  x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
  return x ^ (x >> 31);
}

template <typename T>
struct value_factory;

template <>
struct value_factory<std::uint32_t> {
  static constexpr std::string_view name = "u32";

  [[nodiscard]] static std::uint32_t make(std::uint64_t ordinal) noexcept {
    auto const x = static_cast<std::uint32_t>(ordinal);
    return x * 0x9e3779b1 + 0x85ebca6b;
  }
};

template <>
struct value_factory<std::uint64_t> {
  static constexpr std::string_view name = "u64";

  [[nodiscard]] static std::uint64_t make(std::uint64_t ordinal) noexcept {
    return mix64(ordinal);
  }
};

template <>
struct value_factory<std::string> {
  static constexpr std::string_view name = "string64";

  [[nodiscard]] static std::string make(std::uint64_t ordinal) {
    static constexpr char hex[] = "0123456789abcdef";

    std::string value(64, 'x');
    auto x = mix64(ordinal);
    for (std::size_t i = 0; i < 16; ++i) {
      value[i] = hex[(x >> (4 * i)) & 0xf];
    }

    for (std::size_t block = 1; block < 4; ++block) {
      x = mix64(x + block);
      for (std::size_t i = 0; i < 16; ++i) {
        value[block * 16 + i] = hex[(x >> (4 * i)) & 0xf];
      }
    }

    return value;
  }
};

template <typename T>
[[nodiscard]] std::vector<T>
make_values(std::size_t count, std::uint64_t first = 0) {
  std::vector<T> values;
  values.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    values.emplace_back(value_factory<T>::make(first + i));
  }
  return values;
}

[[nodiscard]] std::vector<std::size_t> make_order(std::size_t count) {
  std::vector<std::size_t> order(count);
  std::iota(order.begin(), order.end(), std::size_t{0});
  std::mt19937_64 rng{0x75e6b4d3a291c80fULL ^ count};
  std::shuffle(order.begin(), order.end(), rng);
  return order;
}

// Use heterogeneous lookup for strings
template <typename T>
struct probe_adapter {
  [[nodiscard]] static T const& get(T const& value) noexcept { return value; }
};

template <>
struct probe_adapter<std::string> {
  [[nodiscard]] static std::string_view get(std::string const& value) noexcept {
    return value;
  }
};

template <typename Store>
void reserve_store(Store& store, std::size_t count) {
  if constexpr (requires { store.reserve(count); }) {
    store.reserve(count);
  }
}

template <typename Store, typename Key, typename Mapped>
void fill_store(Store& store, std::vector<Key> const& keys,
                std::vector<Mapped> const& mapped) {
  reserve_store(store, keys.size());
  for (std::size_t i = 0; i < keys.size(); ++i) {
    store.emplace_back(keys[i], mapped[i]);
  }
}

template <typename Index, typename Key, typename Mapped>
void fill_index(Index& index, std::vector<Key> const& keys,
                std::vector<Mapped> const& mapped) {
  index.reserve(keys.size());
  for (std::size_t i = 0; i < keys.size(); ++i) {
    auto result = index.emplace(keys[i], mapped[i]);
    benchmark::DoNotOptimize(result.index);
  }
}

inline void set_items_processed(benchmark::State& state, std::size_t per_iter) {
  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) *
                          static_cast<std::int64_t>(per_iter));
}

// Bulk insertion with both store and index pre-reserved.
template <typename Config>
void BM_InsertUniqueReserved(benchmark::State& state) {
  using Key = typename Config::key_type;
  using Mapped = typename Config::mapped_type;
  using Store = typename Config::store_type;
  using Index = typename Config::index_type;

  auto const count = static_cast<std::size_t>(state.range(0));
  auto const keys = make_values<Key>(count);
  auto const mapped = make_values<Mapped>(count);

  std::optional<Store> store;
  std::optional<Index> index;

  for (auto _ : state) {
    state.PauseTiming();
    store.emplace();
    index.emplace(*store);
    index->reserve(count);
    state.ResumeTiming();

    for (std::size_t i = 0; i < count; ++i) {
      auto result = index->emplace(keys[i], mapped[i]);
      benchmark::DoNotOptimize(result.index);
      benchmark::DoNotOptimize(result.inserted);
    }
    benchmark::ClobberMemory();

    state.PauseTiming();
    auto const wrong_size = index->size() != count;
    index.reset();
    store.reset();
    state.ResumeTiming();
    if (wrong_size) {
      state.SkipWithError("unique insertion produced the wrong size");
      break;
    }
  }

  set_items_processed(state, count);
}

// Same, but via try_emplace(), which probes before constructing the entry.
template <typename Config>
void BM_TryEmplaceUniqueReserved(benchmark::State& state) {
  using Key = typename Config::key_type;
  using Mapped = typename Config::mapped_type;
  using Store = typename Config::store_type;
  using Index = typename Config::index_type;

  auto const count = static_cast<std::size_t>(state.range(0));
  auto const keys = make_values<Key>(count);
  auto const mapped = make_values<Mapped>(count);

  std::optional<Store> store;
  std::optional<Index> index;

  for (auto _ : state) {
    state.PauseTiming();
    store.emplace();
    index.emplace(*store);
    index->reserve(count);
    state.ResumeTiming();

    for (std::size_t i = 0; i < count; ++i) {
      auto result = index->try_emplace(keys[i], mapped[i]);
      benchmark::DoNotOptimize(result.index);
      benchmark::DoNotOptimize(result.inserted);
    }
    benchmark::ClobberMemory();

    state.PauseTiming();
    auto const wrong_size = index->size() != count;
    index.reset();
    store.reset();
    state.ResumeTiming();
    if (wrong_size) {
      state.SkipWithError("unique insertion produced the wrong size");
      break;
    }
  }

  set_items_processed(state, count);
}

// Insertion without reserve(), so growth/rehashing is included.
template <typename Config>
void BM_InsertUniqueGrowing(benchmark::State& state) {
  using Key = typename Config::key_type;
  using Mapped = typename Config::mapped_type;
  using Store = typename Config::store_type;
  using Index = typename Config::index_type;

  auto const count = static_cast<std::size_t>(state.range(0));
  auto const keys = make_values<Key>(count);
  auto const mapped = make_values<Mapped>(count);

  std::optional<Store> store;
  std::optional<Index> index;

  for (auto _ : state) {
    state.PauseTiming();
    store.emplace();
    index.emplace(*store);
    state.ResumeTiming();

    for (std::size_t i = 0; i < count; ++i) {
      auto result = index->emplace(keys[i], mapped[i]);
      benchmark::DoNotOptimize(result.index);
      benchmark::DoNotOptimize(result.inserted);
    }
    benchmark::ClobberMemory();

    state.PauseTiming();
    auto const wrong_size = index->size() != count;
    index.reset();
    store.reset();
    state.ResumeTiming();
    if (wrong_size) {
      state.SkipWithError("unique insertion produced the wrong size");
      break;
    }
  }

  set_items_processed(state, count);
}

template <typename Config>
void BM_InsertDuplicate(benchmark::State& state) {
  using Key = typename Config::key_type;
  using Mapped = typename Config::mapped_type;
  using Store = typename Config::store_type;
  using Index = typename Config::index_type;

  auto const count = static_cast<std::size_t>(state.range(0));
  auto const keys = make_values<Key>(count);
  auto const mapped = make_values<Mapped>(count);
  auto const order = make_order(count);

  Store store;
  Index index(store);
  fill_index(index, keys, mapped);

  for (auto _ : state) {
    for (auto const i : order) {
      auto result = index.emplace(keys[i], mapped[i]);
      benchmark::DoNotOptimize(result.index);
      benchmark::DoNotOptimize(result.inserted);
    }
  }

  if (index.size() != count) {
    state.SkipWithError("duplicate insertion changed the index size");
  }
  set_items_processed(state, count);
}

// try_emplace() on an existing key never constructs the entry
template <typename Config>
void BM_TryEmplaceDuplicate(benchmark::State& state) {
  using Key = typename Config::key_type;
  using Mapped = typename Config::mapped_type;
  using Store = typename Config::store_type;
  using Index = typename Config::index_type;

  auto const count = static_cast<std::size_t>(state.range(0));
  auto const keys = make_values<Key>(count);
  auto const mapped = make_values<Mapped>(count);
  auto const order = make_order(count);

  Store store;
  Index index(store);
  fill_index(index, keys, mapped);

  for (auto _ : state) {
    for (auto const i : order) {
      decltype(auto) probe = probe_adapter<Key>::get(keys[i]);
      auto result = index.try_emplace(probe, mapped[i]);
      benchmark::DoNotOptimize(result.index);
      benchmark::DoNotOptimize(result.inserted);
    }
  }

  if (index.size() != count) {
    state.SkipWithError("duplicate insertion changed the index size");
  }
  set_items_processed(state, count);
}

// 50% duplicate / 50% new insertions
template <typename Config>
void BM_InsertMixed50(benchmark::State& state) {
  using Key = typename Config::key_type;
  using Mapped = typename Config::mapped_type;
  using Store = typename Config::store_type;
  using Index = typename Config::index_type;

  auto const count = static_cast<std::size_t>(state.range(0));
  auto const half = count / 2;
  auto const keys = make_values<Key>(count);
  auto const mapped = make_values<Mapped>(count);
  auto const duplicate_order = make_order(half);

  std::optional<Store> store;
  std::optional<Index> index;

  for (auto _ : state) {
    state.PauseTiming();
    store.emplace();
    index.emplace(*store);
    index->reserve(count);
    for (std::size_t i = 0; i < half; ++i) {
      index->emplace(keys[i], mapped[i]);
    }
    state.ResumeTiming();

    for (std::size_t i = 0; i < half; ++i) {
      auto const d = duplicate_order[i];
      auto duplicate_result = index->emplace(keys[d], mapped[d]);
      benchmark::DoNotOptimize(duplicate_result.index);
      benchmark::DoNotOptimize(duplicate_result.inserted);

      auto unique_result = index->emplace(keys[half + i], mapped[half + i]);
      benchmark::DoNotOptimize(unique_result.index);
      benchmark::DoNotOptimize(unique_result.inserted);
    }
    benchmark::ClobberMemory();

    state.PauseTiming();
    auto const wrong_size = index->size() != count;
    index.reset();
    store.reset();
    state.ResumeTiming();
    if (wrong_size) {
      state.SkipWithError("mixed insertion produced the wrong size");
      break;
    }
  }

  set_items_processed(state, half * 2);
}

template <typename Config>
void BM_LookupHit(benchmark::State& state) {
  using Key = typename Config::key_type;
  using Mapped = typename Config::mapped_type;
  using Store = typename Config::store_type;
  using Index = typename Config::index_type;

  auto const count = static_cast<std::size_t>(state.range(0));
  auto const keys = make_values<Key>(count);
  auto const mapped = make_values<Mapped>(count);
  auto const order = make_order(count);

  Store store;
  Index index(store);
  fill_index(index, keys, mapped);

  for (auto _ : state) {
    for (auto const i : order) {
      decltype(auto) probe = probe_adapter<Key>::get(keys[i]);
      auto found = index.contains(probe);
      benchmark::DoNotOptimize(found);
    }
  }

  set_items_processed(state, count);
}

template <typename Config>
void BM_LookupMiss(benchmark::State& state) {
  using Key = typename Config::key_type;
  using Mapped = typename Config::mapped_type;
  using Store = typename Config::store_type;
  using Index = typename Config::index_type;

  auto const count = static_cast<std::size_t>(state.range(0));
  auto const keys = make_values<Key>(count);
  auto const mapped = make_values<Mapped>(count);
  auto const misses = make_values<Key>(count, 0x40000000ULL);
  auto const order = make_order(count);

  Store store;
  Index index(store);
  fill_index(index, keys, mapped);

  for (auto _ : state) {
    for (auto const i : order) {
      decltype(auto) probe = probe_adapter<Key>::get(misses[i]);
      auto found = index.contains(probe);
      benchmark::DoNotOptimize(found);
    }
  }

  set_items_processed(state, count);
}

template <typename Config>
void BM_DenseAccessRandom(benchmark::State& state) {
  using Key = typename Config::key_type;
  using Mapped = typename Config::mapped_type;
  using Store = typename Config::store_type;
  using Index = typename Config::index_type;

  auto const count = static_cast<std::size_t>(state.range(0));
  auto const keys = make_values<Key>(count);
  auto const mapped = make_values<Mapped>(count);

  Store store;
  Index index(store);
  fill_index(index, keys, mapped);

  for (auto _ : state) {
    for (auto const& k : keys) {
      auto value = std::addressof(index.mapped(k));
      benchmark::DoNotOptimize(value);
    }
  }

  set_items_processed(state, count);
}

// In-place mutation of the mapped value through the index
template <typename Config>
void BM_MappedUpdateRandom(benchmark::State& state) {
  using Key = typename Config::key_type;
  using Mapped = typename Config::mapped_type;
  using Store = typename Config::store_type;
  using Index = typename Config::index_type;

  auto const count = static_cast<std::size_t>(state.range(0));
  auto const keys = make_values<Key>(count);
  auto const mapped = make_values<Mapped>(count);
  auto const order = make_order(count);

  Store store;
  Index index(store);
  fill_index(index, keys, mapped);

  for (auto _ : state) {
    for (auto const i : order) {
      auto const& k = keys[i];
      index.mapped(k) = mapped[(i + 1) % count];
      benchmark::DoNotOptimize(index.mapped(k));
    }
  }

  set_items_processed(state, count);
}

// Measures construction of the transient index over an already-populated store.
template <typename Config>
void BM_RebuildIndex(benchmark::State& state) {
  using Key = typename Config::key_type;
  using Mapped = typename Config::mapped_type;
  using Store = typename Config::store_type;
  using Index = typename Config::index_type;

  auto const count = static_cast<std::size_t>(state.range(0));
  auto const keys = make_values<Key>(count);
  auto const mapped = make_values<Mapped>(count);

  std::optional<Store> store;
  std::optional<Index> index;

  for (auto _ : state) {
    state.PauseTiming();
    store.emplace();
    fill_store(*store, keys, mapped);
    state.ResumeTiming();

    index.emplace(*store);
    auto rebuilt_size = index->size();
    benchmark::DoNotOptimize(rebuilt_size);
    benchmark::ClobberMemory();

    state.PauseTiming();
    auto const wrong_size = index->size() != count;
    index.reset();
    store.reset();
    state.ResumeTiming();
    if (wrong_size) {
      state.SkipWithError("rebuilt index has the wrong size");
      break;
    }
  }

  set_items_processed(state, count);
}

template <typename Benchmark>
void add_sizes(Benchmark* benchmark) {
  benchmark->Arg(1 << 10)->Arg(1 << 14)->Arg(1 << 18)->ArgName("n");
}

template <typename Config>
void register_config() {
  using Key = typename Config::key_type;
  using Mapped = typename Config::mapped_type;

  auto const suffix = std::string{value_factory<Key>::name} + "->" +
                      std::string{value_factory<Mapped>::name} + "/" +
                      std::string{Config::store_tag::name} + "/" +
                      std::string{Config::index_tag::name};

  auto reg = [&](std::string_view operation, auto fn) {
    auto const name = "DenseMapIndex/" + std::string{operation} + "/" + suffix;
    add_sizes(benchmark::RegisterBenchmark(name.c_str(), fn));
  };

  reg("InsertUniqueReserved", &BM_InsertUniqueReserved<Config>);
  reg("TryEmplaceUniqueReserved", &BM_TryEmplaceUniqueReserved<Config>);
  reg("InsertUniqueGrowing", &BM_InsertUniqueGrowing<Config>);
  reg("InsertDuplicate", &BM_InsertDuplicate<Config>);
  reg("TryEmplaceDuplicate", &BM_TryEmplaceDuplicate<Config>);
  reg("InsertMixed50", &BM_InsertMixed50<Config>);
  reg("LookupHit", &BM_LookupHit<Config>);
  reg("LookupMiss", &BM_LookupMiss<Config>);
  reg("DenseAccessRandom", &BM_DenseAccessRandom<Config>);
  reg("MappedUpdateRandom", &BM_MappedUpdateRandom<Config>);
  reg("RebuildIndex", &BM_RebuildIndex<Config>);
}

template <typename Key, typename Mapped, typename StoreTag>
void register_store() {
  register_config<
      benchmark_config<Key, Mapped, StoreTag, std_unordered_set_tag>>();
  register_config<
      benchmark_config<Key, Mapped, StoreTag, phmap_flat_hash_set_tag>>();
  register_config<benchmark_config<Key, Mapped, StoreTag,
                                   phmap_parallel_flat_hash_set_tag>>();
}

void register_benchmarks() {
  // cheap key, cheap mapped value
  register_store<std::uint32_t, std::uint64_t, vector_store_tag>();
  register_store<std::uint32_t, std::uint64_t, chunked_store_tag>();
  // expensive key
  register_store<std::string, std::uint64_t, vector_store_tag>();
  register_store<std::string, std::uint64_t, chunked_store_tag>();
  // expensive key + mapped value
  register_store<std::string, std::string, vector_store_tag>();
}

} // namespace

int main(int argc, char** argv) {
  register_benchmarks();
  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  return 0;
}
