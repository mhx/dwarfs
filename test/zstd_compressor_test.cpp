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
#include <random>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <dwarfs/binary_literals.h>
#include <dwarfs/block_compressor.h>
#include <dwarfs/malloc_byte_buffer.h>

#include "test_helpers.h"

using namespace dwarfs;
using namespace dwarfs::binary_literals;

TEST(zstd_compressor, long_distance_matching) {
  std::mt19937_64 rng{42};
  auto input = malloc_byte_buffer::create_zeroed(8_MiB);
  auto const random_bytes = test::create_random_string(4_KiB, rng);

  // replace start and end of input with the same random bytes
  auto start = input.span().subspan(0, random_bytes.size());
  auto end = input.span().subspan(input.size() - random_bytes.size(),
                                  random_bytes.size());
  std::ranges::copy(random_bytes, start.begin());
  std::ranges::copy(random_bytes, end.begin());

  {
    auto bc = block_compressor("zstd:level=5");
    auto compressed = bc.compress(input.share());

    EXPECT_GT(compressed.size(), 8_KiB);
    EXPECT_LT(compressed.size(), 9_KiB);
  }

  {
    auto bc = block_compressor("zstd:level=5:long");
    auto compressed = bc.compress(input.share());

    EXPECT_GT(compressed.size(), 4_KiB);
    EXPECT_LT(compressed.size(), 5_KiB);
  }
}

TEST(zstd_compressor, estimate_memory_usage) {
  auto const mem15_32 =
      block_compressor("zstd:level=15").estimate_memory_usage(32_MiB);
  auto const mem15_64 =
      block_compressor("zstd:level=15").estimate_memory_usage(64_MiB);
  auto const mem15long_8 =
      block_compressor("zstd:level=15:long").estimate_memory_usage(8_MiB);
  auto const mem15long_64 =
      block_compressor("zstd:level=15:long").estimate_memory_usage(64_MiB);
  auto const mem22_64 =
      block_compressor("zstd:level=22").estimate_memory_usage(64_MiB);

  EXPECT_LT(mem15_32, mem15_64);
  EXPECT_LT(mem15long_8, mem15long_64);
  EXPECT_LT(mem15_64, mem15long_64);
  EXPECT_LT(mem15_64, mem22_64);

  EXPECT_LT(mem15_32, 150_MiB);
  EXPECT_GT(mem22_64, 500_MiB);
}
