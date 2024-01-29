/* vim:set ts=2 sw=2 sts=2 et: */
/**
 * \author     Marcus Holland-Moritz (github@mhxnet.de)
 * \copyright  Copyright (c) Marcus Holland-Moritz
 *
 * This file is part of ricepp.
 *
 * ricepp is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * ricepp is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with ricepp.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <random>

#include <range/v3/range/conversion.hpp>
#include <range/v3/view/concat.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <ricepp/byteswap.h>
#include <ricepp/ricepp.h>

namespace {

template <std::unsigned_integral ValueType>
std::vector<ValueType>
generate_random_data(size_t count, unsigned unused_lsb_count = 0,
                     std::endian byteorder = std::endian::big,
                     unsigned full_chance = 50) {
  std::mt19937_64 rng(42);
  std::uniform_int_distribution<unsigned> dist(0, full_chance);
  std::uniform_int_distribution<ValueType> noise(20000, 21000);
  std::uniform_int_distribution<ValueType> full(
      0, std::numeric_limits<ValueType>::max());
  std::vector<ValueType> data(count);
  ValueType mask = static_cast<ValueType>(std::numeric_limits<ValueType>::max()
                                          << unused_lsb_count);
  std::generate(data.begin(), data.end(), [&]() {
    return ricepp::byteswap<ValueType>(
        (dist(rng) == 0 ? full(rng) : noise(rng)) & mask, byteorder);
  });
  return data;
}

} // namespace

TEST(ricepp, codec_basic_test) {
  auto codec = ricepp::create_codec<uint16_t>({
      .block_size = 16,
      .component_stream_count = 1,
      .byteorder = std::endian::big,
      .unused_lsb_count = 0,
  });

  auto data = generate_random_data<uint16_t>(12345);
  auto encoded = codec->encode(data);

  std::vector<uint16_t> decoded(data.size());
  codec->decode(decoded, encoded);

  EXPECT_THAT(decoded, ::testing::ContainerEq(data));
}

TEST(ricepp, codec_unused_lsb_test) {
  auto codec = ricepp::create_codec<uint16_t>({
      .block_size = 13, // because why not?
      .component_stream_count = 1,
      .byteorder = std::endian::big,
      .unused_lsb_count = 4,
  });

  auto data = generate_random_data<uint16_t>(4321, 4);
  auto encoded = codec->encode(data);

  std::vector<uint16_t> decoded(data.size());
  codec->decode(decoded, encoded);

  EXPECT_THAT(decoded, ::testing::ContainerEq(data));
}

TEST(ricepp, codec_mixed_data_test) {
  auto codec = ricepp::create_codec<uint16_t>({
      .block_size = 32,
      .component_stream_count = 1,
      .byteorder = std::endian::big,
      .unused_lsb_count = 0,
  });

  auto data1 = generate_random_data<uint16_t>(500, 0);
  auto data2 = std::vector<uint16_t>(500, 25000);
  auto data3 = generate_random_data<uint16_t>(500, 0, std::endian::big, 0);

  auto data = ranges::views::concat(data1, data2, data3) | ranges::to_vector;

  auto encoded = codec->encode(data);

  std::vector<uint16_t> decoded(data.size());
  codec->decode(decoded, encoded);

  EXPECT_THAT(decoded, ::testing::ContainerEq(data));
}

TEST(ricepp, codec_multi_component_test) {
  auto codec = ricepp::create_codec<uint16_t>({
      .block_size = 29,
      .component_stream_count = 2,
      .byteorder = std::endian::big,
      .unused_lsb_count = 2,
  });

  auto data = generate_random_data<uint16_t>(23456, 2);
  auto encoded = codec->encode(data);

  std::vector<uint16_t> decoded(data.size());
  codec->decode(decoded, encoded);

  EXPECT_THAT(decoded, ::testing::ContainerEq(data));
}

TEST(ricepp, codec_preallocated_buffer_test) {
  auto codec = ricepp::create_codec<uint16_t>({
      .block_size = 29,
      .component_stream_count = 1,
      .byteorder = std::endian::big,
      .unused_lsb_count = 0,
  });

  static constexpr size_t const kDataLen = 14443;
  auto data = generate_random_data<uint16_t>(kDataLen, 0, std::endian::big, 0);
  auto worst_case_bytes = codec->worst_case_encoded_bytes(data);
  static constexpr size_t const kWorstCaseBytes = 29138;
  EXPECT_EQ(kWorstCaseBytes, worst_case_bytes);

  std::vector<uint8_t> encoded(worst_case_bytes);
  auto span = codec->encode(encoded, data);
  EXPECT_EQ(kWorstCaseBytes, span.size());
  encoded.resize(span.size());
  encoded.shrink_to_fit();

  std::vector<uint16_t> decoded(data.size());
  codec->decode(decoded, encoded);

  EXPECT_THAT(decoded, ::testing::ContainerEq(data));
}

TEST(ricepp, codec_worst_case_bytes_test) {
  auto codec = ricepp::create_codec<uint16_t>({
      .block_size = 29,
      .component_stream_count = 2,
      .byteorder = std::endian::big,
      .unused_lsb_count = 0,
  });

  static constexpr size_t const kDataLen = 28886;
  auto worst_case_bytes = codec->worst_case_encoded_bytes(kDataLen);
  static constexpr size_t const kWorstCaseBytes = 58275;
  EXPECT_EQ(kWorstCaseBytes, worst_case_bytes);
}

TEST(ricepp, codec_error_test) {
  EXPECT_THAT(
      [] {
        auto codec = ricepp::create_codec<uint16_t>({
            .block_size = 513,
            .component_stream_count = 2,
            .byteorder = std::endian::big,
            .unused_lsb_count = 0,
        });
      },
      ::testing::ThrowsMessage<std::runtime_error>(
          "Unsupported configuration"));

  EXPECT_THAT(
      [] {
        auto codec = ricepp::create_codec<uint16_t>({
            .block_size = 128,
            .component_stream_count = 3,
            .byteorder = std::endian::big,
            .unused_lsb_count = 0,
        });
      },
      ::testing::ThrowsMessage<std::runtime_error>(
          "Unsupported configuration"));
}
