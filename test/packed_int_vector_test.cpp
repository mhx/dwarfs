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

#include <gtest/gtest.h>

#include <dwarfs/internal/packed_int_vector.h>

using namespace dwarfs::internal;

TEST(packed_int_vector, basic) {
  packed_int_vector<uint32_t> vec(5);

  vec.push_back(1);
  vec.push_back(31);
  vec.push_back(0);
  vec.push_back(5);
  vec.push_back(3);
  vec.push_back(25);

  EXPECT_EQ(vec.size(), 6);
  EXPECT_EQ(vec.size_in_bytes(), 4);

  EXPECT_EQ(vec[0], 1);
  EXPECT_EQ(vec[1], 31);
  EXPECT_EQ(vec[2], 0);
  EXPECT_EQ(vec[3], 5);
  EXPECT_EQ(vec[4], 3);
  EXPECT_EQ(vec[5], 25);

  vec[0] = 11;
  EXPECT_EQ(vec[0], 11);

  vec.at(5) = 0;
  EXPECT_EQ(vec[5], 0);

  vec.resize(10);
  EXPECT_EQ(vec[1], 31);

  EXPECT_THROW(vec.at(10), std::out_of_range);
  EXPECT_THROW(vec.at(10) = 17, std::out_of_range);

  auto const& cvec = vec;

  EXPECT_EQ(cvec[0], 11);
  EXPECT_EQ(cvec[5], 0);

  EXPECT_THROW(cvec.at(10), std::out_of_range);

  vec.resize(4);
  vec.shrink_to_fit();

  EXPECT_EQ(vec.capacity(), 6);

  EXPECT_EQ(vec[0], 11);
  EXPECT_FALSE(vec.empty());

  vec.clear();

  EXPECT_EQ(vec.size(), 0);
  EXPECT_TRUE(vec.empty());
  vec.shrink_to_fit();
  EXPECT_EQ(vec.capacity(), 0);
  EXPECT_EQ(vec.size_in_bytes(), 0);
}

TEST(packed_int_vector, signed_int) {
  packed_int_vector<int64_t> vec(13);

  for (int64_t i = -4096; i < 4096; ++i) {
    vec.push_back(i);
  }

  EXPECT_EQ(vec.size(), 8192);
  EXPECT_EQ(vec.size_in_bytes(), 13312);

  EXPECT_EQ(vec.front(), -4096);
  EXPECT_EQ(vec.back(), 4095);

  vec.resize(4096);

  for (int64_t i = 0; i < 4096; ++i) {
    EXPECT_EQ(vec[i], i - 4096);
  }

  auto unpacked = vec.unpack();

  for (int64_t i = 0; i < 4096; ++i) {
    EXPECT_EQ(unpacked[i], i - 4096);
  }
}

TEST(packed_int_vector, zero_bits) {
  packed_int_vector<uint32_t> vec(0);

  for (uint32_t i = 0; i < 100; ++i) {
    vec.push_back(0);
  }

  EXPECT_EQ(vec.size(), 100);
  EXPECT_EQ(vec.size_in_bytes(), 0);

  for (uint32_t i = 0; i < 100; ++i) {
    EXPECT_EQ(vec[i], 0);
  }
}
