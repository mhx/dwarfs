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

#include <cassert>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <random>
#include <span>
#include <string>
#include <vector>

#include <benchmark/benchmark.h>

#include <dwarfs/checksum.h>

using namespace dwarfs;

namespace {

struct buffer_size {
  size_t size;
  char const* name;
  benchmark::TimeUnit unit;
};

constexpr buffer_size const buffer_sizes[]{
    {size_t{4} << 10, "4KiB", benchmark::kNanosecond},
    {size_t{1} << 20, "1MiB", benchmark::kMicrosecond},
    {size_t{256} << 20, "256MiB", benchmark::kMillisecond},
};

constexpr size_t max_buffer_size{
    buffer_sizes[std::size(buffer_sizes) - 1].size};

/**
 * Shared, lazily initialized input buffer.
 *
 * Allocating and filling this on first use rather than at registration time
 * keeps a filtered run (e.g. only the 4KiB cases) from touching 256 MiB of
 * memory. The contents are pseudo-random so we neither benchmark against a
 * page of zeroes nor let the allocator hand us shared zero pages.
 */
std::span<uint8_t const> test_data(size_t size) {
  static std::vector<uint8_t> const data{[] {
    std::vector<uint8_t> buf(max_buffer_size);
    std::mt19937_64 rng{42};
    for (size_t i = 0; i < buf.size(); i += sizeof(uint64_t)) {
      auto const v = rng();
      std::memcpy(buf.data() + i, &v, sizeof(v));
    }
    return buf;
  }()};
  assert(size <= data.size());
  return {data.data(), size};
}

/**
 * Steady-state hashing throughput.
 *
 * The checksum object is constructed once and recycled via reset(), so what's
 * measured is the cost of hashing rather than the cost of allocating an
 * implementation. See bm_checksum_construct() below for the difference.
 */
void bm_checksum_reset(benchmark::State& state, std::string const& alg,
                       size_t size) {
  auto const data = test_data(size);

  checksum cs(alg);
  std::vector<uint8_t> digest(cs.digest_size());

  for ([[maybe_unused]] auto _ : state) {
    cs.update(std::as_bytes(data));
    cs.finalize(digest.data());
    benchmark::DoNotOptimize(digest.data());
    benchmark::ClobberMemory();
    cs.reset();
  }

  state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                          static_cast<int64_t>(data.size()));
  state.SetLabel(alg);
}

/**
 * Same as above, but constructing a fresh checksum for every digest.
 *
 * Only registered for the smallest buffer, where the per-object setup is
 * actually visible next to the hashing itself.
 */
void bm_checksum_construct(benchmark::State& state, std::string const& alg,
                           size_t size) {
  auto const data = test_data(size);

  std::vector<uint8_t> digest(checksum(alg).digest_size());

  for ([[maybe_unused]] auto _ : state) {
    checksum cs(alg);
    cs.update(std::as_bytes(data));
    cs.finalize(digest.data());
    benchmark::DoNotOptimize(digest.data());
    benchmark::ClobberMemory();
  }

  state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                          static_cast<int64_t>(data.size()));
  state.SetLabel(alg);
}

void register_benchmarks() {
  for (auto const& alg : checksum::available_algorithms()) {
    for (auto const& bs : buffer_sizes) {
      auto const size = bs.size;
      auto const name = "checksum/" + alg + "/" + bs.name;

      benchmark::RegisterBenchmark(name.c_str(), [alg, size](
                                                     benchmark::State& state) {
        bm_checksum_reset(state, alg, size);
      })->Unit(bs.unit);
    }

    // the per-object setup cost is only visible at the smallest size
    auto const& small = buffer_sizes[0];
    auto const name = "checksum_construct/" + alg + "/" + small.name;

    benchmark::RegisterBenchmark(name.c_str(), [alg](benchmark::State& state) {
      bm_checksum_construct(state, alg, buffer_sizes[0].size);
    })->Unit(small.unit);
  }
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
