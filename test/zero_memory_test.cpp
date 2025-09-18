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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <dwarfs/binary_literals.h>
#include <dwarfs/zero_memory.h>

using namespace dwarfs;
using namespace dwarfs::binary_literals;

TEST(zero_memory, basic) {
  auto zeroes = zero_memory::create(8_MiB);
  auto span = zeroes.span();

  EXPECT_EQ(zeroes.size(), 8_MiB);
  EXPECT_EQ(span.size(), 8_MiB);
  EXPECT_TRUE(std::ranges::all_of(span, [](auto b) { return b == 0; }));
}
