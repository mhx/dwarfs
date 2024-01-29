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

#include <algorithm>
#include <array>
#include <concepts>
#include <cstdint>
#include <random>

#include <gtest/gtest.h>

#include <folly/json.h>
#include <folly/lang/Bits.h>

#include <range/v3/range/conversion.hpp>
#include <range/v3/view/concat.hpp>

#include "dwarfs/block_compressor.h"

using namespace dwarfs;

namespace {

template <std::unsigned_integral ValueType>
std::vector<ValueType>
generate_random_data(std::mt19937_64& rng, size_t count,
                     unsigned unused_lsb_count = 0, unsigned full_chance = 50) {
  std::uniform_int_distribution<unsigned> dist(0, full_chance);
  std::uniform_int_distribution<ValueType> noise(30000, 31000);
  std::uniform_int_distribution<ValueType> full(
      0, std::numeric_limits<ValueType>::max());
  std::vector<ValueType> data(count);
  ValueType mask = static_cast<ValueType>(std::numeric_limits<ValueType>::max()
                                          << unused_lsb_count);
  std::generate(data.begin(), data.end(), [&]() {
    auto v = dist(rng) == 0 ? full(rng) : noise(rng);
    return folly::Endian::big<ValueType>(v & mask);
  });
  return data;
}

template <std::unsigned_integral ValueType>
std::vector<uint8_t>
make_test_data(int components, int pixels, int unused_lsb) {
  std::mt19937_64 rng(42);
  std::uniform_int_distribution<ValueType> any_value(
      0, std::numeric_limits<ValueType>::max());
  std::vector<std::vector<ValueType>> data(components);

  auto random_value = [&]() {
    return folly::Endian::big<ValueType>(any_value(rng) << unused_lsb);
  };

  for (auto& d : data) {
    auto d1 = generate_random_data<ValueType>(rng, pixels / 3, unused_lsb);
    auto d2 = std::vector<ValueType>(pixels / 3, random_value());
    auto d3 = generate_random_data<ValueType>(
        rng, pixels - (d1.size() + d2.size()), unused_lsb, 0);
    d = ranges::views::concat(d1, d2, d3) | ranges::to_vector;
  }

  if (data.size() < 1 || data.size() > 2) {
    throw std::runtime_error("invalid number of components");
  }

  std::vector<ValueType> tmp;
  tmp.resize(data.size() * data[0].size());

  for (size_t i = 0; i < data[0].size(); ++i) {
    for (size_t j = 0; j < data.size(); ++j) {
      tmp[i * data.size() + j] = data[j][i];
    }
  }

  std::vector<uint8_t> out;
  out.resize(tmp.size() * sizeof(ValueType));
  std::memcpy(out.data(), tmp.data(), out.size());

  return out;
}

struct data_params {
  data_params(int components, int pixels, int unused, int block = 0)
      : num_components{components}
      , num_pixels{pixels}
      , unused_lsb{unused}
      , block_size{block} {}

  int num_components;
  int num_pixels;
  int unused_lsb;
  int block_size;
};

std::ostream& operator<<(std::ostream& os, data_params const& p) {
  os << "{comp=" << p.num_components << ", pix=" << p.num_pixels
     << ", lsb=" << p.unused_lsb << ", block=" << p.block_size << "}";
  return os;
}

std::vector<data_params> const data_parameters{
    // clang-format off
    { 1, 1000, 0, 16 },
    { 2, 1000, 2, 32 },
    { 1, 1000, 4, 64 },
    { 2, 3333, 6, 99 },
    // clang-format on
};

} // namespace

class ricepp_param : public testing::TestWithParam<data_params> {};

TEST_P(ricepp_param, combinations) {
  auto param = GetParam();

  folly::dynamic meta = folly::dynamic::object;
  meta.insert("endianness", "big");
  meta.insert("bytes_per_sample", 2);
  meta.insert("unused_lsb_count", param.unused_lsb);
  meta.insert("component_count", param.num_components);

  auto const data = make_test_data<uint16_t>(
      param.num_components, param.num_pixels, param.unused_lsb);

  block_compressor comp(fmt::format("ricepp:block_size={}", param.block_size));

  auto compressed = comp.compress(data, folly::toJson(meta));

  EXPECT_LT(compressed.size(), 7 * data.size() / 10);

  auto decompressed = block_decompressor::decompress(
      compression_type::RICEPP, compressed.data(), compressed.size());

  ASSERT_EQ(data.size(), decompressed.size());
  EXPECT_EQ(data, decompressed);
}

INSTANTIATE_TEST_SUITE_P(dwarfs, ricepp_param,
                         ::testing::ValuesIn(data_parameters));
