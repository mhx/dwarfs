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
#include <random>

#include <benchmark/benchmark.h>

#include <dwarfs/writer/internal/cyclic_hash.h>

namespace {

using namespace dwarfs::writer::internal;

std::vector<uint8_t> make_random_data(size_t size) {
  std::vector<uint8_t> data(size);
  std::mt19937_64 rng;
  std::uniform_int_distribution<uint8_t> dist;
  std::generate(data.begin(), data.end(), [&] { return dist(rng); });
  return data;
}

std::vector<uint8_t> const random_data() {
  static std::vector<uint8_t> data = make_random_data(16 * 1024 * 1024);
  return data;
}

void rsync_hash_1k(::benchmark::State& state) {
  auto const& data = random_data();
  for (auto _ : state) {
    static constexpr size_t kWindowSize = 1024;
    size_t pos = 0;
    size_t end = data.size() - kWindowSize;
    rsync_hash hash;

    while (pos < kWindowSize) {
      hash.update(data[pos]);
      ++pos;
    }

    while (pos < end) {
      hash.update(data[pos - kWindowSize], data[pos]);
      benchmark::DoNotOptimize(hash());
      ++pos;
    }
  }
  state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                          data.size());
}

void parallel_cyclic_hash_1k(::benchmark::State& state) {
  auto const& data = random_data();
  for (auto _ : state) {
    static constexpr size_t kWindowSize = 1024;
    size_t pos = 0;
    size_t end = data.size() - kWindowSize;
    parallel_cyclic_hash<uint32_t> hash(kWindowSize);

    while (pos < kWindowSize) {
      uint32_t in = reinterpret_cast<uint32_t const*>(
          data.data())[pos / sizeof(uint32_t)];
      hash.update_wide(in);
      pos += sizeof(uint32_t);
    }

    while (pos < end) {
      uint32_t in = reinterpret_cast<uint32_t const*>(
          data.data())[pos / sizeof(uint32_t)];
      uint32_t out = reinterpret_cast<uint32_t const*>(
          data.data())[(pos - kWindowSize) / sizeof(uint32_t)];
      hash.update_wide(out, in);
      benchmark::DoNotOptimize(hash(0));
      benchmark::DoNotOptimize(hash(1));
      benchmark::DoNotOptimize(hash(2));
      benchmark::DoNotOptimize(hash(3));
      pos += sizeof(uint32_t);
    }
  }
  state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                          data.size());
}

void cyclic_hash_sse_1k(::benchmark::State& state) {
  auto const& data = random_data();
  for (auto _ : state) {
    static constexpr size_t kWindowSize = 1024;
    size_t pos = 0;
    size_t end = data.size() - kWindowSize;
    cyclic_hash_sse hash(kWindowSize);

    while (pos < kWindowSize) {
      uint32_t in = reinterpret_cast<uint32_t const*>(
          data.data())[pos / sizeof(uint32_t)];
      hash.update_wide(in);
      pos += sizeof(uint32_t);
    }

    std::array<uint32_t, 4> hv;

    while (pos < end) {
      uint32_t in = reinterpret_cast<uint32_t const*>(
          data.data())[pos / sizeof(uint32_t)];
      uint32_t out = reinterpret_cast<uint32_t const*>(
          data.data())[(pos - kWindowSize) / sizeof(uint32_t)];
      hash.update_wide(out, in);
      hash.get(hv.data());
      benchmark::DoNotOptimize(hv[0]);
      benchmark::DoNotOptimize(hv[1]);
      benchmark::DoNotOptimize(hv[2]);
      benchmark::DoNotOptimize(hv[3]);
      pos += sizeof(uint32_t);
    }
  }
  state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                          data.size());
}

void cyclic_hash_sse_1k_narrow(::benchmark::State& state) {
  auto const& data = random_data();
  for (auto _ : state) {
    static constexpr size_t kWindowSize = 1024;
    size_t pos = 0;
    size_t end = data.size() - kWindowSize;
    cyclic_hash_sse hash(kWindowSize);

    while (pos < kWindowSize) {
      hash.update(data[pos]);
      ++pos;
    }

    std::array<uint32_t, 4> hv;

    while (pos < end) {
      // uint32_t in = reinterpret_cast<uint32_t const*>(
      //     data.data())[pos / sizeof(uint32_t)];
      // uint32_t out = reinterpret_cast<uint32_t const*>(
      //     data.data())[(pos - kWindowSize) / sizeof(uint32_t)];
      // hash.update_wide(out, in);
      hash.update(data[pos - kWindowSize], data[pos]);
      if (pos % sizeof(uint32_t) == 0) {
        hash.get(hv.data());
        benchmark::DoNotOptimize(hv[0]);
        benchmark::DoNotOptimize(hv[1]);
        benchmark::DoNotOptimize(hv[2]);
        benchmark::DoNotOptimize(hv[3]);
      }
      ++pos;
    }
  }
  state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                          data.size());
}

} // namespace

BENCHMARK(rsync_hash_1k)->Unit(benchmark::kMillisecond);

BENCHMARK(parallel_cyclic_hash_1k)->Unit(benchmark::kMillisecond);

BENCHMARK(cyclic_hash_sse_1k)->Unit(benchmark::kMillisecond);
BENCHMARK(cyclic_hash_sse_1k_narrow)->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();
