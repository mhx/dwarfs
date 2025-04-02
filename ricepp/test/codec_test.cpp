/* vim:set ts=2 sw=2 sts=2 et: */
/**
 * \author     Marcus Holland-Moritz (github@mhxnet.de)
 * \copyright  Copyright (c) Marcus Holland-Moritz
 *
 * This file is part of ricepp.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the “Software”), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */

#include <random>

#include <range/v3/range/conversion.hpp>
#include <range/v3/view/concat.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <ricepp/byteswap.h>
#include <ricepp/create_decoder.h>
#include <ricepp/create_encoder.h>

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
  auto config = ricepp::codec_config{
      .block_size = 16,
      .component_stream_count = 1,
      .byteorder = std::endian::big,
      .unused_lsb_count = 0,
  };

  auto encoder = ricepp::create_encoder<uint16_t>(config);

  auto data = generate_random_data<uint16_t>(12345);
  auto encoded = encoder->encode(data);

  auto decoder = ricepp::create_decoder<uint16_t>(config);

  std::vector<uint16_t> decoded(data.size());
  decoder->decode(decoded, encoded);

  EXPECT_THAT(decoded, ::testing::ContainerEq(data));
}

TEST(ricepp, codec_unused_lsb_test) {
  auto config = ricepp::codec_config{
      .block_size = 13, // because why not?
      .component_stream_count = 1,
      .byteorder = std::endian::big,
      .unused_lsb_count = 4,
  };

  auto encoder = ricepp::create_encoder<uint16_t>(config);

  auto data = generate_random_data<uint16_t>(4321, 4);
  auto encoded = encoder->encode(data);

  auto decoder = ricepp::create_decoder<uint16_t>(config);

  std::vector<uint16_t> decoded(data.size());
  decoder->decode(decoded, encoded);

  EXPECT_THAT(decoded, ::testing::ContainerEq(data));
}

TEST(ricepp, codec_mixed_data_test) {
  auto config = ricepp::codec_config{
      .block_size = 32,
      .component_stream_count = 1,
      .byteorder = std::endian::big,
      .unused_lsb_count = 0,
  };

  auto encoder = ricepp::create_encoder<uint16_t>(config);

  auto data1 = generate_random_data<uint16_t>(500, 0);
  auto data2 = std::vector<uint16_t>(500, 25000);
  auto data3 = generate_random_data<uint16_t>(500, 0, std::endian::big, 0);

  auto data = ranges::views::concat(data1, data2, data3) | ranges::to_vector;

  auto encoded = encoder->encode(data);

  auto decoder = ricepp::create_decoder<uint16_t>(config);

  std::vector<uint16_t> decoded(data.size());
  decoder->decode(decoded, encoded);

  EXPECT_THAT(decoded, ::testing::ContainerEq(data));
}

TEST(ricepp, codec_multi_component_test) {
  auto config = ricepp::codec_config{
      .block_size = 29,
      .component_stream_count = 2,
      .byteorder = std::endian::big,
      .unused_lsb_count = 2,
  };

  auto encoder = ricepp::create_encoder<uint16_t>(config);

  auto data = generate_random_data<uint16_t>(23456, 2);
  auto encoded = encoder->encode(data);

  auto decoder = ricepp::create_decoder<uint16_t>(config);

  std::vector<uint16_t> decoded(data.size());
  decoder->decode(decoded, encoded);

  EXPECT_THAT(decoded, ::testing::ContainerEq(data));
}

TEST(ricepp, codec_preallocated_buffer_test) {
  auto config = ricepp::codec_config{
      .block_size = 29,
      .component_stream_count = 1,
      .byteorder = std::endian::big,
      .unused_lsb_count = 0,
  };

  auto encoder = ricepp::create_encoder<uint16_t>(config);

  static constexpr size_t const kDataLen = 14443;
  auto data = generate_random_data<uint16_t>(kDataLen, 0, std::endian::big, 0);
  auto worst_case_bytes = encoder->worst_case_encoded_bytes(data);
  static constexpr size_t const kWorstCaseBytes = 29138;
  EXPECT_EQ(kWorstCaseBytes, worst_case_bytes);

  std::vector<uint8_t> encoded(worst_case_bytes);
  auto span = encoder->encode(encoded, data);
  EXPECT_EQ(kWorstCaseBytes, span.size());
  encoded.resize(span.size());
  encoded.shrink_to_fit();

  auto decoder = ricepp::create_decoder<uint16_t>(config);

  std::vector<uint16_t> decoded(data.size());
  decoder->decode(decoded, encoded);

  EXPECT_THAT(decoded, ::testing::ContainerEq(data));
}

TEST(ricepp, encoder_worst_case_bytes_test) {
  auto encoder = ricepp::create_encoder<uint16_t>({
      .block_size = 29,
      .component_stream_count = 2,
      .byteorder = std::endian::big,
      .unused_lsb_count = 0,
  });

  static constexpr size_t const kDataLen = 28886;
  auto worst_case_bytes = encoder->worst_case_encoded_bytes(kDataLen);
  static constexpr size_t const kWorstCaseBytes = 58275;
  EXPECT_EQ(kWorstCaseBytes, worst_case_bytes);
}

TEST(ricepp, codec_error_test) {
  EXPECT_THAT(
      [] {
        auto encoder = ricepp::create_encoder<uint16_t>({
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
        auto decoder = ricepp::create_decoder<uint16_t>({
            .block_size = 128,
            .component_stream_count = 3,
            .byteorder = std::endian::big,
            .unused_lsb_count = 0,
        });
      },
      ::testing::ThrowsMessage<std::runtime_error>(
          "Unsupported configuration"));
}
