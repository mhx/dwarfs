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

#include <cmath>
#include <numbers>

#include <gtest/gtest.h>

#include <folly/json.h>

#include "dwarfs/block_compressor.h"
#include "dwarfs/pcm_sample_transformer.h"

using namespace dwarfs;

namespace {

template <typename T>
std::vector<T> make_sine(int bits, size_t length, double period) {
  std::vector<T> rv(length);
  double amplitude = (1 << bits) / 2;
  for (size_t i = 0; i < length; ++i) {
    rv[i] = static_cast<T>(
        amplitude * std::sin(2 * std::numbers::pi * i / period) - 0.5);
  }
  return rv;
}

template <typename T>
std::vector<T> multiplex(std::vector<std::vector<T>> const& in) {
  auto samples = in.front().size();
  auto channels = in.size();
  std::vector<T> out(channels * samples);

  for (size_t i = 0; i < samples; ++i) {
    for (size_t c = 0; c < channels; ++c) {
      out[i * channels + c] = in.at(c).at(i);
    }
  }

  return out;
}

template <typename T = int32_t>
std::vector<uint8_t>
make_test_data(int channels, int samples, int bytes, int bits,
               pcm_sample_endianness end, pcm_sample_signedness sig,
               pcm_sample_padding pad) {
  std::vector<std::vector<T>> data;
  for (int c = 0; c < channels; ++c) {
    data.emplace_back(
        make_sine<T>(bits, samples, 3.1 * ((599 * (c + 1)) % 256)));
  }
  auto muxed = multiplex(data);
  std::vector<uint8_t> out(bytes * channels * samples);
  pcm_sample_transformer<T> xfm(end, sig, pad, bytes, bits);
  xfm.pack(out, muxed);
  return out;
}

struct data_params {
  data_params(int channels, int samples, int bytes, int bits)
      : num_channels{channels}
      , num_samples{samples}
      , bytes_per_sample{bytes}
      , bits_per_sample{bits} {}

  int num_channels;
  int num_samples;
  int bytes_per_sample;
  int bits_per_sample;
};

std::ostream& operator<<(std::ostream& os, data_params const& p) {
  os << "{channels=" << p.num_channels << ", samples=" << p.num_samples
     << ", bytes=" << p.bytes_per_sample << ", bits=" << p.bits_per_sample
     << "}";
  return os;
}

std::vector<data_params> const data_parameters{
    // clang-format off
    { 1,   1000, 2, 16},
    { 3,   1000, 1, 8},
    { 1,   1000, 2, 12},
    { 1, 100000, 3, 20},
    { 8,  10000, 3, 20},
    { 4,  10000, 4, 20},
    { 4,  10000, 4, 24},
    { 4,  10000, 3, 24},
    { 7, 799999, 4, 32},
    // clang-format on
};

} // namespace

TEST(flac_compressor, sine) {
  {
    auto test = make_sine<int8_t>(8, 5, 4.0);
    std::vector<int8_t> ref{0, 127, 0, -128, 0};
    EXPECT_EQ(test, ref);
  }
  {
    auto test = make_sine<int8_t>(5, 5, 4.0);
    std::vector<int8_t> ref{0, 15, 0, -16, 0};
    EXPECT_EQ(test, ref);
  }
  {
    auto test = make_sine<int16_t>(16, 5, 4.0);
    std::vector<int16_t> ref{0, 32767, 0, -32768, 0};
    EXPECT_EQ(test, ref);
  }
  {
    auto test = make_sine<int16_t>(12, 5, 4.0);
    std::vector<int16_t> ref{0, 2047, 0, -2048, 0};
    EXPECT_EQ(test, ref);
  }
}

TEST(flac_compressor, basic) {
  folly::dynamic meta = folly::dynamic::object;
  meta.insert("endianness", "little");
  meta.insert("signedness", "signed");
  meta.insert("padding", "msb");
  meta.insert("bytes_per_sample", 2);
  meta.insert("bits_per_sample", 16);
  meta.insert("number_of_channels", 2);

  auto const data =
      make_test_data(2, 1000, 2, 16, pcm_sample_endianness::Little,
                     pcm_sample_signedness::Signed, pcm_sample_padding::Msb);

  block_compressor comp("flac");

  auto compressed = comp.compress(data, folly::toJson(meta));

  EXPECT_LT(compressed.size(), data.size() / 2);

  auto decompressed = block_decompressor::decompress(
      compression_type::FLAC, compressed.data(), compressed.size());

  EXPECT_EQ(data, decompressed);
}

class flac_param : public testing::TestWithParam<
                       std::tuple<pcm_sample_endianness, pcm_sample_signedness,
                                  pcm_sample_padding, data_params>> {};

TEST_P(flac_param, combinations) {
  auto [end, sig, pad, param] = GetParam();

  folly::dynamic meta = folly::dynamic::object;
  meta.insert("endianness",
              end == pcm_sample_endianness::Big ? "big" : "little");
  meta.insert("signedness",
              sig == pcm_sample_signedness::Signed ? "signed" : "unsigned");
  meta.insert("padding", pad == pcm_sample_padding::Msb ? "msb" : "lsb");
  meta.insert("bytes_per_sample", param.bytes_per_sample);
  meta.insert("bits_per_sample", param.bits_per_sample);
  meta.insert("number_of_channels", param.num_channels);

  auto const data = make_test_data(param.num_channels, param.num_samples,
                                   param.bytes_per_sample,
                                   param.bits_per_sample, end, sig, pad);

  block_compressor comp("flac");

  auto compressed = comp.compress(data, folly::toJson(meta));

  EXPECT_LT(compressed.size(), data.size() / 2);

  auto decompressed = block_decompressor::decompress(
      compression_type::FLAC, compressed.data(), compressed.size());

  EXPECT_EQ(data, decompressed);
}

INSTANTIATE_TEST_SUITE_P(
    dwarfs, flac_param,
    ::testing::Combine(::testing::Values(pcm_sample_endianness::Big,
                                         pcm_sample_endianness::Little),
                       ::testing::Values(pcm_sample_signedness::Signed,
                                         pcm_sample_signedness::Unsigned),
                       ::testing::Values(pcm_sample_padding::Lsb,
                                         pcm_sample_padding::Msb),
                       ::testing::ValuesIn(data_parameters)));
