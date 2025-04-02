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
#include <string_view>

#include <gtest/gtest.h>

#include <fmt/format.h>

#include <dwarfs/writer/internal/nilsimsa.h>

namespace {

using namespace dwarfs::writer::internal;
using namespace std::string_view_literals;

class nilsimsa_tester {
 public:
  void update(std::string_view data) {
    ns_.update(reinterpret_cast<uint8_t const*>(data.data()), data.size());
  }

  std::string digest() {
    nilsimsa::hash_type hash;
    ns_.finalize(hash);
    return fmt::format("{:016x}{:016x}{:016x}{:016x}", hash[3], hash[2],
                       hash[1], hash[0]);
  }

  static std::string hash(std::string_view data) {
    nilsimsa_tester ns;
    ns.update(data);
    return ns.digest();
  }

 private:
  nilsimsa ns_;
};

} // namespace

TEST(nilisimsa, empty) {
  EXPECT_EQ(nilsimsa_tester::hash(""),
            "0000000000000000000000000000000000000000000000000000000000000000");
}

TEST(nilisimsa, abcdefgh) {
  EXPECT_EQ(nilsimsa_tester::hash("abcdefgh"),
            "14c8118000000000030800000004042004189020001308014088003280000078");
}

TEST(nilisimsa, incremental) {
  nilsimsa_tester ns;

  ns.update("a");
  ns.update("bc");
  ns.update("defgh");
  EXPECT_EQ(ns.digest(),
            "14c8118000000000030800000004042004189020001308014088003280000078");

  ns.update("i");
  ns.update("jk");
  EXPECT_EQ(ns.digest(),
            "14c811840010000c0328200108040630041890200217582d4098103280000078");
}

TEST(nilisimsa, moreabc) {
  constexpr auto input = "abcdefghijklmnopqrstuvwxyz"sv;
  constexpr std::array<std::string_view, input.size()> expected{
      "0000000000000000000000000000000000000000000000000000000000000000",
      "0000000000000000000000000000000000000000000000000000000000000000",
      "0040000000000000000000000000000000000000000000000000000000000000",
      "0440000000000000000000000000000000100000000000000008000000000000",
      "0440008000000000000000000000000000100020001200000008001200000050",
      "04c0018000000000000000000000000004188020001200000088001280000058",
      "04c8118000000000030000000000002004188020001208004088001280000078",
      "14c8118000000000030800000004042004189020001308014088003280000078",
      "14c8118400000000030800010804043004189020021318094098003280000078",
      "14c81184000000000308200108040430041890200217580d4098103280000078",
      "14c811840010000c0328200108040630041890200217582d4098103280000078",
      "14c811840010000ca328200108044630041890200a17586d4298103280000078",
      "14ca11850010000ca328200188044630041898200a17586dc2d8103284000078",
      "14ca11850030004ca3a8200188044630041898200a17586dc2d8107284000078",
      "14ca11850032004ca3a8284188044730041898200a17586dc2d8107384000078",
      "94ca11850432005ca3a828418804473004199c200a17586dc2d8107384004178",
      "94ca11850433005ca3a82841880447341419be200a17586dc2d8107384004178",
      "94ca11850433005ca3a82841a88457341419be201a17586dc6d8107384084178",
      "94ca11850533005ca3b82841a88657361419be201a17586dc6d8107384084178",
      "94ca11850533005ca3b82841aa8657371419be201a17587dc6d81077840c4178",
      "94ca15850533005ca3b92841aa8657371419be201a17587dd6d81077844cc178",
      "94ca15850533005ca3b92849aa8657371419be201a17587fd6d81077844cc978",
      "94ca15850533045cabb92869aa8657371419bea01a17587fd6f81077c44cc978",
      "94ca95850533045cabb93869aa8657371499beb01a17587fd6f8107fc44cc978",
      "94ca95850733045cabb93869aa8657373499beb01a17587fd6f9107fc54cc978",
      "94ca95850773045cabb93869ba8657373499beb81a17587fd6f9107fc54cc978",
  };

  for (size_t i = 0; i < input.size(); ++i) {
    EXPECT_EQ(nilsimsa_tester::hash(input.substr(0, i + 1)), expected[i])
        << "i=" << i;
  }
}
