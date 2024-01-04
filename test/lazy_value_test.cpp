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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "dwarfs/lazy_value.h"

using namespace dwarfs;

TEST(lazy_value_test, basic) {
  int num_calls = 0;
  lazy_value<int> v([&] {
    ++num_calls;
    return 42;
  });
  EXPECT_EQ(0, num_calls);
  EXPECT_EQ(42, v.get());
  EXPECT_EQ(1, num_calls);
  EXPECT_EQ(42, v.get());
  EXPECT_EQ(1, num_calls);
  EXPECT_EQ(42, v());
  EXPECT_EQ(1, num_calls);
}
