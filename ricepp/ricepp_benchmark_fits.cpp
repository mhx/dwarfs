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

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <latch>
#include <optional>
#include <sstream>

#include <benchmark/benchmark.h>

#include <ricepp/byteswap.h>
#include <ricepp/create_decoder.h>
#include <ricepp/create_encoder.h>

namespace {

std::filesystem::path const g_testdata_dir{
    "/home/mhx/git/github/dwarfs/@ricepp-testdata"};

struct config {
  unsigned component_stream_count;
  unsigned unused_lsb_count;
};

std::map<std::string, config> const g_camera_info = {
    {"ASI178MC", {2, 0}},  {"ASI294MC", {2, 2}},  {"ASI1600MM", {1, 4}},
    {"ASI2600MC", {2, 0}}, {"ASI2600MM", {1, 0}}, {"ASI6200MC", {2, 0}},
};

std::string format_percentage(double value) {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(2) << "(" << value << "%)";
  return oss.str();
}

class ricepp_bm : public ::benchmark::Fixture {
 public:
  void SetUp(::benchmark::State const& state) {
    if (state.thread_index() > 0) {
      latch_->wait();
      return;
    }

    auto const [camera, test, operation] = [&] {
      std::string name{state.name()};
      auto const pos1 = name.find('/');
      auto const pos2 = name.find('/', pos1 + 1);
      return std::make_tuple(name.substr(0, pos1),
                             name.substr(pos1 + 1, pos2 - pos1 - 1),
                             name.substr(pos2 + 1));
    }();

    auto const& camera_info = g_camera_info.at(camera);

    auto config = ricepp::codec_config{
        .block_size = 128,
        .component_stream_count = camera_info.component_stream_count,
        .byteorder = std::endian::big,
        .unused_lsb_count = camera_info.unused_lsb_count,
    };

    encoder_ = ricepp::create_encoder<uint16_t>(config);
    decoder_ = ricepp::create_decoder<uint16_t>(config);

    if (data_.empty()) {
      std::filesystem::path testdata_dir;
      if (auto dir = std::getenv("RICEPP_FITS_TESTDATA_DIR")) {
        testdata_dir = dir;
      } else {
        testdata_dir = g_testdata_dir;
      }

      data_ = load_fits_data(testdata_dir / camera / (test + ".fit"));

      encoded_ = encoder_->encode(data_);
    }

    latch_->count_down();
  }

  void TearDown(::benchmark::State const& state) {
    latch_.reset();
    latch_.emplace(1);
  }

  std::vector<uint16_t> load_fits_data(std::filesystem::path const& path) {
    static constexpr size_t kBlockSize = 2880;
    static constexpr size_t kDataSize = 8 * 1024 * 1024;

    std::ifstream ifs{path, std::ios::binary};
    if (!ifs) {
      throw std::runtime_error{"failed to open file: " + path.string()};
    }

    // skip a bunch of header blocks
    ifs.seekg(8 * kBlockSize, std::ios::beg);

    std::vector<uint16_t> data;
    data.resize(kDataSize / sizeof(data[0]));
    ifs.read(reinterpret_cast<char*>(data.data()), kDataSize);

    if (!ifs) {
      throw std::runtime_error{"failed to read data from file: " +
                               path.string()};
    }

    return data;
  }

  std::unique_ptr<ricepp::encoder_interface<uint16_t>> encoder_;
  std::unique_ptr<ricepp::decoder_interface<uint16_t>> decoder_;
  std::vector<uint16_t> data_;
  std::vector<uint8_t> encoded_;
  std::optional<std::latch> latch_{1};
};

} // namespace

#define RICEPP_BENCHMARK(camera, test)                                         \
  BENCHMARK_DEFINE_F(ricepp_bm, encode_##camera##_##test)                      \
  (::benchmark::State & state) {                                               \
    thread_local std::vector<uint8_t> encoded;                                 \
    encoded.resize(encoder_->worst_case_encoded_bytes(data_));                 \
    for (auto _ : state) {                                                     \
      auto r = encoder_->encode(encoded, data_);                               \
      ::benchmark::DoNotOptimize(r);                                           \
    }                                                                          \
    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *         \
                            data_.size() * sizeof(data_[0]));                  \
    state.SetLabel(format_percentage(100.0 * encoded_.size() /                 \
                                     (data_.size() * sizeof(data_[0]))));      \
  }                                                                            \
  BENCHMARK_DEFINE_F(ricepp_bm, decode_##camera##_##test)                      \
  (::benchmark::State & state) {                                               \
    thread_local std::vector<uint16_t> decoded;                                \
    decoded.resize(data_.size());                                              \
    for (auto _ : state) {                                                     \
      decoder_->decode(decoded, encoded_);                                     \
    }                                                                          \
    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *         \
                            data_.size() * sizeof(data_[0]));                  \
  }                                                                            \
  BENCHMARK_REGISTER_F(ricepp_bm, encode_##camera##_##test)                    \
      ->Unit(benchmark::kMillisecond)                                          \
      ->ThreadRange(1, 8)                                                      \
      ->UseRealTime()                                                          \
      ->Name(#camera "/" #test "/encode");                                     \
  BENCHMARK_REGISTER_F(ricepp_bm, decode_##camera##_##test)                    \
      ->Unit(benchmark::kMillisecond)                                          \
      ->ThreadRange(1, 8)                                                      \
      ->UseRealTime()                                                          \
      ->Name(#camera "/" #test "/decode");

RICEPP_BENCHMARK(ASI1600MM, dark_120s_g139)
RICEPP_BENCHMARK(ASI1600MM, dark_1s_g0)
RICEPP_BENCHMARK(ASI1600MM, flat_h_2s_g0)
RICEPP_BENCHMARK(ASI1600MM, light_g_60s_g0)
RICEPP_BENCHMARK(ASI1600MM, light_s_120s_g139)
RICEPP_BENCHMARK(ASI178MC, bias_g0)
RICEPP_BENCHMARK(ASI178MC, dark_60s_g0)
RICEPP_BENCHMARK(ASI178MC, flat_g0)
RICEPP_BENCHMARK(ASI178MC, light_60s_g0)
RICEPP_BENCHMARK(ASI2600MC, flat_g0)
RICEPP_BENCHMARK(ASI2600MC, light_180s_g100)
RICEPP_BENCHMARK(ASI2600MM, bias_g0)
RICEPP_BENCHMARK(ASI2600MM, bias_g100)
RICEPP_BENCHMARK(ASI2600MM, bias_g300)
RICEPP_BENCHMARK(ASI2600MM, dark_900s_g300)
RICEPP_BENCHMARK(ASI2600MM, flat_h_g0)
RICEPP_BENCHMARK(ASI2600MM, light_b_30s_g0)
RICEPP_BENCHMARK(ASI2600MM, light_h_120s_g300)
RICEPP_BENCHMARK(ASI294MC, light_120s_g200)
RICEPP_BENCHMARK(ASI294MC, light_180s_g0)
RICEPP_BENCHMARK(ASI294MC, light_60s_g0)
RICEPP_BENCHMARK(ASI6200MC, light_60s_g100)

BENCHMARK_MAIN();
