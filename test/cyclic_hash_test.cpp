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

#include <cstring>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <fmt/format.h>

#include <dwarfs/writer/internal/cyclic_hash.h>

using namespace dwarfs::writer::internal;

TEST(cyclic_hash_test, parallel) {
  std::array<char, 53> input1{
      {"The quick brown fox jumps over the lazy dog in time."}};
  std::array<char, 53> input2{
      {"Our not so quick brown fox jumps over the furry cat."}};
  // std::array<char, 53> input1{
  //     {"xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"}};
  // std::array<char, 53> input2{
  //     {"xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"}};
  // std::array<char, 53>
  // input1{{"\0abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXY"}};
  // std::array<char, 53>
  // input2{{"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"}};
  static size_t constexpr kWindowSize = 16;
  parallel_cyclic_hash<uint32_t> hash1(kWindowSize);
  parallel_cyclic_hash<uint32_t> hash2(kWindowSize);
  cyclic_hash_sse hash3(kWindowSize);
  cyclic_hash_sse hash4(kWindowSize);

  for (size_t i = 0; i < input1.size(); i += sizeof(uint32_t)) {
    uint32_t in1, in2;
    size_t const num = std::min(input1.size() - i, sizeof(uint32_t));
    std::memcpy(&in1, input1.data() + i, num);
    std::memcpy(&in2, input2.data() + i, num);
    if (i < kWindowSize) {
      std::cout << "-----\n";
      hash1.update_wide(in1);
      hash2.update_wide(in2);
      hash3.update_wide(in1);
      for (size_t j = 0; j < sizeof(uint32_t); ++j) {
        hash4.update(input2[i + j]);
      }
    } else {
      std::cout << "=====\n";
      uint32_t out1, out2;
      std::memcpy(&out1, input1.data() + (i - kWindowSize), sizeof(uint32_t));
      std::memcpy(&out2, input2.data() + (i - kWindowSize), sizeof(uint32_t));
      hash1.update_wide(out1, in1);
      hash2.update_wide(out2, in2);
      hash3.update_wide(out1, in1);
      for (size_t j = 0; j < sizeof(uint32_t); ++j) {
        hash4.update(input2[i + j - kWindowSize], input2[i + j]);
      }
    }

    std::array<uint32_t, 4> h3v, h4v;
    hash3.get(h3v.data());
    hash4.get(h4v.data());

    for (size_t j = 0; j < sizeof(uint32_t); ++j) {
      auto h1 = hash1(j);
      auto h2 = hash2(j);
      auto h3 = h3v[j];
      auto h4 = h4v[j];

      std::cout << fmt::format("{:02d}  {:}  {:}  {:08x}  {:08x}  {:08x}  {:08x}{}\n",
                               i + j, input1[i + j], input2[i + j], h1, h3, h2, h4,
                               h1 != h3 || h2 != h4 ? "  <---" : "");
    }
  }
}

TEST(cyclic_hash_test, repeating_window) {
  for (int window_bits = 4; window_bits < 8; ++window_bits) {
    size_t const window_size = 1 << window_bits;

    for (unsigned byteval = 0; byteval < 256; ++byteval) {
      uint32_t const inval = byteval + (byteval << 8) + (byteval << 16) +
                             (byteval << 24);

      parallel_cyclic_hash<uint32_t> hash1(window_size);
      cyclic_hash_sse hash2(window_size);

      for (size_t i = 0; i < window_size; i += sizeof(uint32_t)) {
        hash1.update_wide(inval);
        hash2.update_wide(inval);
      }

      std::array<uint32_t, 4> h1v, h2v;
      hash1.get(h1v.data());
      hash2.get(h2v.data());

      std::cout << fmt::format("===== window_size = {:02d}  byteval = {:02x}\n",
                               window_size, byteval);

      std::cout << fmt::format("{:08x}  {:08x}  {:08x}  {:08x}\n", h1v[0], h1v[1], h1v[2], h1v[3]);
      std::cout << fmt::format("{:08x}  {:08x}  {:08x}  {:08x}\n", h2v[0], h2v[1], h2v[2], h2v[3]);

      std::cout << "=====\n";

      uint32_t const expected = cyclic_hash_sse::repeating_window(byteval, window_size);

      EXPECT_EQ(expected, h1v[3]);
      EXPECT_EQ(expected, h2v[3]);

      for (size_t i = 0; i < 128; i += sizeof(uint32_t)) {
        hash1.update_wide(inval, inval);
        hash2.update_wide(inval, inval);
        hash1.get(h1v.data());
        hash2.get(h2v.data());
        std::cout << fmt::format("{:08x}  {:08x}  {:08x}  {:08x}    {:08x}\n", h1v[0], h1v[1], h1v[2], h1v[3], expected);
        std::cout << fmt::format("{:08x}  {:08x}  {:08x}  {:08x}\n", h2v[0], h2v[1], h2v[2], h2v[3]);
        std::cout << "-----\n";

        for (size_t i = 0; i < 4; ++i) {
          EXPECT_EQ(expected, h1v[i]);
          EXPECT_EQ(expected, h2v[i]);
        }
      }
    }
  }
}
