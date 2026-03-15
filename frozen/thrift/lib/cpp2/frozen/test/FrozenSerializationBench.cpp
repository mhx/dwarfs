/*
 * SPDX-FileCopyrightText: Copyright (c) Meta Platforms, Inc. and affiliates.
 * SPDX-FileCopyrightText: Copyright (c) Marcus Holland-Moritz
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * This file is derived from fbthrift and has been modified by
 * Marcus Holland-Moritz for use in dwarfs.
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <benchmark/benchmark.h>

#include <thrift/lib/cpp2/frozen/FrozenUtil.h>
#include <thrift/lib/cpp2/frozen/test/gen-cpp-lite/Example_layouts.h>
#include <thrift/lib/cpp2/frozen/test/gen-cpp-lite/Example_types.h>

#include <dwarfs/thrift_lite/compact_reader.h>
#include <dwarfs/thrift_lite/compact_writer.h>

using namespace apache::thrift;
using namespace apache::thrift::frozen;
using namespace apache::thrift::test;

namespace {

EveryLayout stress_value2 = [] {
  EveryLayout x;
  *x.aBool() = true;
  *x.aInt() = 2;
  *x.aList() = {3, 5};
  *x.aSet() = {7, 11};
  *x.aHashSet() = {13, 17};
  *x.aMap() = {{19, 23}, {29, 31}};
  *x.aHashMap() = {{37, 41}, {43, 47}};
  x.optInt() = 53;
  *x.aFloat() = 59.61;
  x.optMap() = {{2, 4}, {3, 9}};
  return x;
}();

void compact_serialize(benchmark::State& state) {
  std::size_t total_size = 0;

  for (auto _ : state) {
    std::vector<std::byte> out;
    dwarfs::thrift_lite::compact_writer writer(out);
    stress_value2.write(writer);
    total_size += out.size();
    benchmark::DoNotOptimize(out);
  }

  benchmark::DoNotOptimize(total_size);
  state.SetBytesProcessed(
      static_cast<int64_t>(
          state.iterations() * total_size / state.iterations()));
}

void frozen_freeze(benchmark::State& state) {
  Layout<EveryLayout> layout;
  LayoutRoot::layout(stress_value2, layout);

  std::size_t total_size = 0;

  for (auto _ : state) {
    std::string out = freezeDataToString(stress_value2, layout);
    total_size += out.size();
    benchmark::DoNotOptimize(out);
  }

  benchmark::DoNotOptimize(total_size);
  state.SetBytesProcessed(
      static_cast<int64_t>(
          state.iterations() * total_size / state.iterations()));
}

void frozen_freeze_preallocate(benchmark::State& state) {
  Layout<EveryLayout> layout;
  LayoutRoot::layout(stress_value2, layout);

  std::size_t total_size = 0;

  for (auto _ : state) {
    std::array<byte, 1024> buffer;
    auto write = std::span<uint8_t>(
        reinterpret_cast<uint8_t*>(buffer.data()), buffer.size());
    ByteRangeFreezer::freeze(layout, stress_value2, write);
    total_size += buffer.size() - write.size();
    benchmark::DoNotOptimize(buffer);
  }

  benchmark::DoNotOptimize(total_size);
  state.SetBytesProcessed(
      static_cast<int64_t>(
          state.iterations() * total_size / state.iterations()));
}

void compact_deserialize(benchmark::State& state) {
  std::vector<std::byte> serialized;
  {
    dwarfs::thrift_lite::compact_writer writer(serialized);
    stress_value2.write(writer);
  }

  std::size_t total_size = 0;

  for (auto _ : state) {
    EveryLayout copy;
    dwarfs::thrift_lite::compact_reader reader(serialized);
    copy.read(reader);
    total_size += serialized.size();
    benchmark::DoNotOptimize(copy);
  }

  benchmark::DoNotOptimize(total_size);
  state.SetBytesProcessed(
      static_cast<int64_t>(state.iterations() * serialized.size()));
}

void frozen_thaw(benchmark::State& state) {
  Layout<EveryLayout> layout;
  LayoutRoot::layout(stress_value2, layout);
  std::string frozen = freezeDataToString(stress_value2, layout);

  std::size_t total_size = 0;

  for (auto _ : state) {
    EveryLayout copy;
    layout.thaw(ViewPosition{reinterpret_cast<byte*>(frozen.data()), 0}, copy);
    total_size += frozen.size();
    benchmark::DoNotOptimize(copy);
  }

  benchmark::DoNotOptimize(total_size);
  state.SetBytesProcessed(
      static_cast<int64_t>(state.iterations() * frozen.size()));
}

void register_benchmarks() {
  benchmark::RegisterBenchmark("CompactSerialize", compact_serialize);
  benchmark::RegisterBenchmark("FrozenFreeze", frozen_freeze);
  benchmark::RegisterBenchmark(
      "FrozenFreezePreallocate", frozen_freeze_preallocate);
  benchmark::RegisterBenchmark("CompactDeserialize", compact_deserialize);
  benchmark::RegisterBenchmark("FrozenThaw", frozen_thaw);
}

} // namespace

int main(int argc, char** argv) {
  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }

  register_benchmarks();
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  return 0;
}

#if 0
Old results using folly-benchmark:

clang version 21.1.8, i9-13900K

                               [folly::Bits]        [dwarfs::bit_view]
=======================================================================
FrozenSerializationBench.cpp   time/iter  iters/s    time/iter  iters/s
=======================================================================
CompactSerialize                266.55ns    3.75M     258.52ns    3.87M
FrozenFreeze                    896.30ns    1.12M     848.98ns    1.18M
FrozenFreezePreallocate         340.53ns    2.94M     327.70ns    3.05M
-----------------------------------------------------------------------
CompactDeserialize              441.23ns    2.27M     452.37ns    2.21M
FrozenThaw                      283.28ns    3.53M     269.96ns    3.70M
#endif
