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

#include <iostream>
#include <random>

#include <benchmark/benchmark.h>

#include <ricepp/byteswap.h>
#include <ricepp/create_decoder.h>
#include <ricepp/create_encoder.h>

namespace {

struct random_config {
  std::endian byteorder{std::endian::big};
  unsigned unused_lsb{0};
  unsigned noise_bits{0};
  unsigned full_bits{0};
  double full_freq{0.0};
};

template <std::unsigned_integral ValueType>
std::vector<ValueType> generate_random_data(std::mt19937_64& rng, size_t count,
                                            random_config const& cfg) {
  std::exponential_distribution<double> full_freq_dist(cfg.full_freq);
  std::uniform_int_distribution<ValueType> noise(
      0, (UINT64_C(1) << (cfg.noise_bits + cfg.unused_lsb)) - 1);
  std::uniform_int_distribution<ValueType> full(
      0, (UINT64_C(1) << (cfg.full_bits + cfg.unused_lsb)) - 1);
  std::vector<ValueType> data(count);
  ValueType mask = static_cast<ValueType>(std::numeric_limits<ValueType>::max()
                                          << cfg.unused_lsb);
  std::generate(data.begin(), data.end(), [&]() {
    return ricepp::byteswap<ValueType>(
        (full_freq_dist(rng) <= 1.0 ? full(rng) : noise(rng)) & mask,
        cfg.byteorder);
  });
  return data;
}

class ricepp_bm : public ::benchmark::Fixture {
 public:
  void SetUp(::benchmark::State const& state) {
    if (state.thread_index() > 0) {
      return;
    }

    std::mt19937_64 rng(42);

    data_ = generate_random_data<uint16_t>(
        rng, state.range(0),
        {
            .byteorder =
                state.range(1) ? std::endian::big : std::endian::little,
            .unused_lsb = static_cast<unsigned>(state.range(2)),
            .noise_bits = static_cast<unsigned>(state.range(3)),
            .full_bits = static_cast<unsigned>(state.range(4)),
            .full_freq = 1.0 / state.range(5),
        });

    auto config = ricepp::codec_config{
        .block_size = static_cast<size_t>(state.range(6)),
        .component_stream_count = static_cast<unsigned>(state.range(7)),
        .byteorder = state.range(1) ? std::endian::big : std::endian::little,
        .unused_lsb_count = static_cast<unsigned>(state.range(2)),
    };

    encoder_ = ricepp::create_encoder<uint16_t>(config);
    decoder_ = ricepp::create_decoder<uint16_t>(config);

    encoded_ = encoder_->encode(data_);
  }

  void TearDown(::benchmark::State const&) {}

  std::unique_ptr<ricepp::encoder_interface<uint16_t>> encoder_;
  std::unique_ptr<ricepp::decoder_interface<uint16_t>> decoder_;
  std::vector<uint16_t> data_;
  std::vector<uint8_t> encoded_;
};

void ricepp_params(benchmark::internal::Benchmark* b) {
  b->ArgNames({"size", "bo", "ulsb", "noise", "full", "freq", "bs", "cs"});
  for (int64_t size : {1024 * 1024, 8 * 1024 * 1024}) {
    for (int bo : {0, 1}) {
      for (int cs : {1, 2}) {
        b->Args({size, bo, 0, 6, 16, 10, 128, cs});
      }
    }
  }

  for (int64_t bs : {16, 32, 64, 128, 256, 512}) {
    b->Args({1024 * 1024, 1, 0, 6, 16, 10, bs, 1});
  }

  for (int64_t full_freq : {2, 4, 8, 16, 32}) {
    b->Args({1024 * 1024, 1, 0, 6, 16, full_freq, 128, 1});
  }

  // b->Args({1024*1024, 1, 0, 6, 16, 10, 64, 1});
  // b->Args({1024*1024, 1, 4, 6, 12, 10, 64, 1});
}

} // namespace

BENCHMARK_DEFINE_F(ricepp_bm, encode)(::benchmark::State& state) {
  std::vector<uint8_t> encoded;
  encoded.resize(encoder_->worst_case_encoded_bytes(data_));
  for (auto _ : state) {
    auto r = encoder_->encode(encoded, data_);
    ::benchmark::DoNotOptimize(r);
  }
  state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                          data_.size() * sizeof(data_[0]));
}

BENCHMARK_DEFINE_F(ricepp_bm, decode)(::benchmark::State& state) {
  std::vector<uint16_t> decoded;
  decoded.resize(data_.size());
  for (auto _ : state) {
    decoder_->decode(decoded, encoded_);
  }
  state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                          data_.size() * sizeof(data_[0]));
}

BENCHMARK_REGISTER_F(ricepp_bm, encode)
    ->Unit(benchmark::kMillisecond)
    ->Apply(ricepp_params)
    ->ThreadRange(1, 4)
    ->UseRealTime();

BENCHMARK_REGISTER_F(ricepp_bm, decode)
    ->Unit(benchmark::kMillisecond)
    ->Apply(ricepp_params)
    ->ThreadRange(1, 4)
    ->UseRealTime();

BENCHMARK_MAIN();
