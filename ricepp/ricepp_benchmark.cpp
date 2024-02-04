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

#include <iostream>
#include <random>

#include <benchmark/benchmark.h>

#include <ricepp/byteswap.h>
#include <ricepp/ricepp.h>

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

    codec_ = ricepp::create_codec<uint16_t>({
        .block_size = static_cast<size_t>(state.range(6)),
        .component_stream_count = static_cast<unsigned>(state.range(7)),
        .byteorder = state.range(1) ? std::endian::big : std::endian::little,
        .unused_lsb_count = static_cast<unsigned>(state.range(2)),
    });

    encoded_ = codec_->encode(data_);
  }

  void TearDown(::benchmark::State const&) {}

  std::unique_ptr<ricepp::codec_interface<uint16_t>> codec_;
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
  encoded.resize(codec_->worst_case_encoded_bytes(data_));
  for (auto _ : state) {
    auto r = codec_->encode(encoded, data_);
    ::benchmark::DoNotOptimize(r);
  }
  state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                          data_.size() * sizeof(data_[0]));
}

BENCHMARK_DEFINE_F(ricepp_bm, decode)(::benchmark::State& state) {
  std::vector<uint16_t> decoded;
  decoded.resize(data_.size());
  for (auto _ : state) {
    codec_->decode(decoded, encoded_);
  }
  state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                          data_.size() * sizeof(data_[0]));
}

BENCHMARK_REGISTER_F(ricepp_bm, encode)
    ->Unit(benchmark::kMillisecond)
    ->Apply(ricepp_params);

BENCHMARK_REGISTER_F(ricepp_bm, decode)
    ->Unit(benchmark::kMillisecond)
    ->Apply(ricepp_params);

BENCHMARK_MAIN();
