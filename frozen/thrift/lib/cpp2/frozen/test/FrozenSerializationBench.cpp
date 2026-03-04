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

#include <folly/Benchmark.h>
#include <thrift/lib/cpp2/frozen/FrozenUtil.h>
#include <thrift/lib/cpp2/frozen/test/gen-cpp-lite/Example_layouts.h>
#include <thrift/lib/cpp2/frozen/test/gen-cpp-lite/Example_types.h>

#include <dwarfs/thrift_lite/compact_reader.h>
#include <dwarfs/thrift_lite/compact_writer.h>

using namespace apache::thrift;
using namespace apache::thrift::frozen;
using namespace apache::thrift::test;

EveryLayout stressValue2 = [] {
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

BENCHMARK(CompactSerialize, iters) {
  size_t s = 0;

  while (iters--) {
    std::vector<std::byte> out;
    dwarfs::thrift_lite::compact_writer w(out);
    stressValue2.write(w);
    s += out.size();
  }
  folly::doNotOptimizeAway(s);
}

BENCHMARK_RELATIVE(FrozenFreeze, iters) {
  size_t s = 0;
  folly::BenchmarkSuspender setup;
  Layout<EveryLayout> layout;
  LayoutRoot::layout(stressValue2, layout);
  setup.dismiss();
  while (iters--) {
    std::string out = freezeDataToString(stressValue2, layout);
    s += out.size();
  }
  folly::doNotOptimizeAway(s);
}

BENCHMARK_RELATIVE(FrozenFreezePreallocate, iters) {
  size_t s = 0;
  folly::BenchmarkSuspender setup;
  Layout<EveryLayout> layout;
  LayoutRoot::layout(stressValue2, layout);
  setup.dismiss();
  while (iters--) {
    std::array<byte, 1024> buffer;
    auto write = std::span<uint8_t>(buffer.begin(), buffer.end());
    ByteRangeFreezer::freeze(layout, stressValue2, write);
    s += buffer.size() - write.size();
  }
  folly::doNotOptimizeAway(s);
}

BENCHMARK_DRAW_LINE();

BENCHMARK(CompactDeserialize, iters) {
  size_t s = 0;
  folly::BenchmarkSuspender setup;
  std::vector<std::byte> out;
  dwarfs::thrift_lite::compact_writer w(out);
  stressValue2.write(w);
  setup.dismiss();

  while (iters--) {
    EveryLayout copy;
    dwarfs::thrift_lite::compact_reader r(out);
    copy.read(r);
    s += out.size();
  }
  folly::doNotOptimizeAway(s);
}

BENCHMARK_RELATIVE(FrozenThaw, iters) {
  size_t s = 0;
  folly::BenchmarkSuspender setup;
  Layout<EveryLayout> layout;
  LayoutRoot::layout(stressValue2, layout);
  std::string out = freezeDataToString(stressValue2, layout);
  setup.dismiss();
  while (iters--) {
    EveryLayout copy;
    layout.thaw(ViewPosition{reinterpret_cast<byte*>(&out[0]), 0}, copy);
    s += out.size();
  }
  folly::doNotOptimizeAway(s);
}

#if 0
clang version 21.1.8, i9-13900K

============================================================================
[...]zen/test/FrozenSerializationBench.cpp     relative  time/iter   iters/s
============================================================================
CompactSerialize                                          266.55ns     3.75M
FrozenFreeze                                    29.739%   896.30ns     1.12M
FrozenFreezePreallocate                         78.275%   340.53ns     2.94M
----------------------------------------------------------------------------
CompactDeserialize                                        441.23ns     2.27M
FrozenThaw                                      155.76%   283.28ns     3.53M
#endif
int main(int, char** argv) {
  google::InitGoogleLogging(argv[0]);
  folly::runBenchmarks();
  return 0;
}
