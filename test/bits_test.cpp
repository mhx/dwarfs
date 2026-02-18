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

#include <cstdint>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <dwarfs/bits.h>

using namespace dwarfs;

TEST(bits, log2_bit_ceil) {
  EXPECT_EQ(0, log2_bit_ceil(0u));
  EXPECT_EQ(0, log2_bit_ceil(1u));
  EXPECT_EQ(1, log2_bit_ceil(2u));
  EXPECT_EQ(2, log2_bit_ceil(3u));
  EXPECT_EQ(10, log2_bit_ceil(513u));

  EXPECT_EQ(10, log2_bit_ceil(1023u));
  EXPECT_EQ(10, log2_bit_ceil(1024u));
  EXPECT_EQ(11, log2_bit_ceil(1025u));

  EXPECT_EQ(10, log2_bit_ceil((UINT64_C(1) << 10) - 1));
  EXPECT_EQ(10, log2_bit_ceil(UINT64_C(1) << 10));
  EXPECT_EQ(11, log2_bit_ceil((UINT64_C(1) << 10) + 1));

  EXPECT_EQ(50, log2_bit_ceil((UINT64_C(1) << 50) - 1));
  EXPECT_EQ(50, log2_bit_ceil(UINT64_C(1) << 50));
  EXPECT_EQ(51, log2_bit_ceil((UINT64_C(1) << 50) + 1));
}
