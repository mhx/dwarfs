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

#include <dwarfs/writer/internal/nilsimsa.h>

namespace {

using namespace dwarfs::writer::internal;

std::vector<uint8_t> random_byte_vector(size_t len) {
  static std::mt19937 g{42};
  static std::uniform_int_distribution<uint16_t> d{0, 255};
  std::vector<uint8_t> v(len);
  std::generate(v.begin(), v.end(), [&] { return d(g); });
  return v;
}

template <size_t N>
void nilsimsa_bytes(::benchmark::State& state) {
  auto input = random_byte_vector(N);

  for (auto _ : state) {
    nilsimsa ns;
    nilsimsa::hash_type h;
    ns.update(input.data(), input.size());
    ns.finalize(h);
    benchmark::DoNotOptimize(h);
  }

  state.SetItemsProcessed(state.iterations() * N);
}

} // namespace

BENCHMARK(nilsimsa_bytes<32>);
BENCHMARK(nilsimsa_bytes<1024>);
BENCHMARK(nilsimsa_bytes<1024 * 32>);
BENCHMARK(nilsimsa_bytes<1024 * 1024>);

BENCHMARK_MAIN();
