/*
 * SPDX-FileCopyrightText: Copyright (c) Meta Platforms, Inc. and affiliates.
 * SPDX-FileCopyrightText: Copyright (c) Marcus Holland-Moritz
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * This file is derived from fbthrift and has been modified by
 * Marcus Holland-Moritz for use in dwarfs.
 */

#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

#include <dwarfs/compiler.h>

#include <thrift/lib/cpp2/frozen/Frozen.h>
#include <thrift/lib/cpp2/frozen/FrozenUtil.h>

using namespace apache::thrift::frozen;

TEST(FrozenIntegral, UIntBounds) {
  EXPECT_EQ(0, frozenSize(0UL));
  EXPECT_EQ(1, frozenSize(0xFFUL));
  EXPECT_EQ(2, frozenSize(0x0100UL));
  EXPECT_EQ(2, frozenSize(0xFFFFUL));
  EXPECT_EQ(3, frozenSize(0x010000UL));
  EXPECT_EQ(3, frozenSize(0x010000UL));
  EXPECT_EQ(7, frozenSize(0xFFFFFFFFFFFFFFUL));
  EXPECT_EQ(8, frozenSize(0x0100000000000000UL));
  EXPECT_EQ(8, frozenSize(0xFFFFFFFFFFFFFFFFUL));
  EXPECT_EQ(0, frozenSize(std::numeric_limits<uint8_t>::min()));
  EXPECT_EQ(1, frozenSize(std::numeric_limits<uint8_t>::max()));
  EXPECT_EQ(0, frozenSize(std::numeric_limits<uint16_t>::min()));
  EXPECT_EQ(2, frozenSize(std::numeric_limits<uint16_t>::max()));
  EXPECT_EQ(0, frozenSize(std::numeric_limits<uint32_t>::min()));
  EXPECT_EQ(4, frozenSize(std::numeric_limits<uint32_t>::max()));
  EXPECT_EQ(0, frozenSize(std::numeric_limits<uint64_t>::min()));
  EXPECT_EQ(8, frozenSize(std::numeric_limits<uint64_t>::max()));
}

TEST(FrozenIntegral, IntBounds) {
  EXPECT_EQ(0, frozenSize(0L));
  EXPECT_EQ(1, frozenSize(0x7FL));
  EXPECT_EQ(1, frozenSize(-0x80L));
  EXPECT_EQ(2, frozenSize(0x80L));
  EXPECT_EQ(2, frozenSize(0x7FFFL));
  EXPECT_EQ(2, frozenSize(-0x8000L));
  EXPECT_EQ(3, frozenSize(0x8000L));
  EXPECT_EQ(7, frozenSize(0x7FFFFFFFFFFFFFL));
  EXPECT_EQ(7, frozenSize(-0x80000000000000L));
  EXPECT_EQ(8, frozenSize(0x7FFFFFFFFFFFFFFFL));
  EXPECT_EQ(8, frozenSize(0x0L - 0x8000000000000000L));
  EXPECT_EQ(8, frozenSize(std::numeric_limits<int64_t>::min()));
  EXPECT_EQ(8, frozenSize(std::numeric_limits<int64_t>::max()));
  EXPECT_EQ(4, frozenSize(std::numeric_limits<int32_t>::min()));
  EXPECT_EQ(4, frozenSize(std::numeric_limits<int32_t>::max()));
  EXPECT_EQ(2, frozenSize(std::numeric_limits<int16_t>::min()));
  EXPECT_EQ(2, frozenSize(std::numeric_limits<int16_t>::max()));
  EXPECT_EQ(1, frozenSize(std::numeric_limits<int8_t>::min()));
  EXPECT_EQ(1, frozenSize(std::numeric_limits<int8_t>::max()));
}

TEST(FrozenIntegral, UIntPacking) {
  class DummyFreezer : public FreezeRoot {
   private:
    void doAppendBytes(
        byte*, size_t, std::span<uint8_t>&, size_t&, size_t) override {}
  };
  DummyFreezer fr;
  size_t value = 5;
  size_t width = 3;
  for (size_t start = 0; start <= 64 - width; start += 7) {
    for (size_t bits = width; bits <= 64 - start; bits += 5) {
      DWARFS_PUSH_WARNING
      DWARFS_GCC_DISABLE_WARNING("-Warray-bounds")
      apache::thrift::frozen::Layout<size_t> l;
      l.bits = bits;
      // allocate on the heap so ASAN will catch out of bounds accesses
      auto container = std::make_unique<uint64_t>(0xDEADBEEFDEADBEEF);
      FreezePosition fpos{reinterpret_cast<byte*>(container.get()), start};
      l.freeze(fr, value, fpos);
      ViewPosition vpos{reinterpret_cast<byte*>(container.get()), start};
      size_t confirm;
      l.thaw(vpos, confirm);
      EXPECT_EQ(value, confirm) << bits << "@" << start;
      DWARFS_POP_WARNING
    }
  }
}
