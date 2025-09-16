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
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <random>
#include <vector>

#include <benchmark/benchmark.h>

#include <dwarfs/compression_constraints.h>
#include <dwarfs/writer/segmenter.h>
#include <dwarfs/writer/writer_progress.h>

#include <dwarfs/writer/internal/block_manager.h>
#include <dwarfs/writer/internal/chunkable.h>

#include "loremipsum.h"
#include "mmap_mock.h"
#include "test_logger.h"

namespace {

class bench_chunkable : public dwarfs::writer::internal::chunkable {
 public:
  explicit bench_chunkable(std::vector<uint8_t> data)
      : mm_{dwarfs::test::make_mock_file_view(
            std::string{data.begin(), data.end()})} {}

  dwarfs::writer::internal::file const* get_file() const override {
    return nullptr;
  }

  dwarfs::file_size_t size() const override { return mm_.size(); }

  std::string description() const override { return std::string(); }

  dwarfs::file_extents_iterable extents() const override {
    return mm_.extents();
  }

  void
  add_chunk(size_t /*block*/, size_t /*offset*/, size_t /*size*/) override {}

  void add_hole(dwarfs::file_size_t /*size*/) override {}

  std::error_code release_until(size_t /*offset*/) override { return {}; }

 private:
  dwarfs::file_view mm_;
};

std::vector<uint8_t>
build_data(size_t total_size, size_t granularity, double dupe_fraction,
           std::initializer_list<size_t> dupe_sizes) {
  std::vector<uint8_t> data;
  data.reserve(total_size);

  std::independent_bits_engine<std::mt19937_64,
                               std::numeric_limits<uint8_t>::digits, uint16_t>
      rng;

  auto granular_size = [granularity](size_t size) {
    return size - (size % granularity);
  };

  auto make_random = [&rng](size_t size) {
    std::vector<uint8_t> v;
    v.resize(size);
    std::generate(begin(v), end(v), std::ref(rng));
    return v;
  };

  std::vector<std::vector<uint8_t>> dupes;
  size_t total_dupe_size{0};
  for (auto s : dupe_sizes) {
    auto gs = granular_size(s);
    dupes.emplace_back(make_random(gs));
    total_dupe_size += gs;
  }

  size_t num_dupes = (total_size * dupe_fraction) / total_dupe_size;
  size_t rand_size = total_size - num_dupes * total_dupe_size;
  size_t avg_rand_size =
      num_dupes > 0 ? granular_size(rand_size / (num_dupes * dupe_sizes.size()))
                    : 0;

  // std::cerr << num_dupes << std::endl;

  auto append_data = [&data](std::vector<uint8_t> const& tmp) {
    data.resize(data.size() + tmp.size());
    std::copy(begin(tmp), end(tmp), end(data) - tmp.size());
  };

  for (size_t i = 0; i < num_dupes * dupe_sizes.size(); ++i) {
    append_data(dupes[i % dupe_sizes.size()]);
    append_data(make_random(avg_rand_size));
  }

  if (data.size() > total_size) {
    throw std::runtime_error(
        fmt::format("internal error: {} > {}", data.size(), total_size));
  }

  append_data(make_random(total_size - data.size()));

  return data;
}

void run_segmenter_benchmark(::benchmark::State& state, unsigned granularity,
                             unsigned window_size, unsigned block_size,
                             unsigned bloom_filter_size, unsigned lookback,
                             double dupe_fraction) {
  dwarfs::writer::segmenter::config cfg;
  cfg.blockhash_window_size = window_size;
  cfg.window_increment_shift = 1;
  cfg.max_active_blocks = lookback;
  cfg.bloom_filter_size = bloom_filter_size;
  cfg.block_size_bits = block_size;

  dwarfs::compression_constraints cc;
  cc.granularity = granularity;

  size_t total_size = 512 * 1024 * 1024;

  bench_chunkable bc(
      build_data(total_size, granularity, dupe_fraction,
                 {2 * granularity * (size_t(1) << window_size)}));

  std::vector<dwarfs::shared_byte_buffer> written;
  size_t segmented{0};

  for (auto _ : state) {
    dwarfs::test::test_logger lgr;
    dwarfs::writer::writer_progress prog;
    auto blkmgr = std::make_shared<dwarfs::writer::internal::block_manager>();
    written.clear();

    dwarfs::writer::segmenter seg(
        lgr, prog, blkmgr, cfg, cc, total_size,
        [&written, blkmgr](dwarfs::shared_byte_buffer blk,
                           auto logical_block_num) {
          auto physical_block_num = written.size();
          written.push_back(blk);
          blkmgr->set_written_block(logical_block_num, physical_block_num, {});
        });

    // begin benchmarking code

    seg.add_chunkable(bc);
    seg.finish();

    // end benchmarking code

    for (auto const& blk : written) {
      segmented += blk.size();
    }
  }

  state.SetBytesProcessed(state.iterations() * total_size);
  state.SetLabel(fmt::format(
      "-- {:.1f} MiB -> {:.1f} MiB ({:.1f}%)", total_size / (1024.0 * 1024.0),
      segmented / (1024.0 * 1024.0), 100.0 * segmented / total_size));
}

constexpr unsigned const kDefaultGranularity{1};
constexpr unsigned const kDefaultWindowSize{12};
constexpr unsigned const kDefaultBlockSize{24};
constexpr unsigned const kDefaultBloomFilterSize{4};
constexpr unsigned const kDefaultLookback{1};
constexpr double const kDefaultDupeFraction{0.3};

template <unsigned Granularity>
void granularity(::benchmark::State& state) {
  run_segmenter_benchmark(state, Granularity, kDefaultWindowSize,
                          kDefaultBlockSize, kDefaultBloomFilterSize,
                          kDefaultLookback, kDefaultDupeFraction);
}

template <unsigned WindowSize>
void window_size(::benchmark::State& state) {
  run_segmenter_benchmark(state, kDefaultGranularity, WindowSize,
                          kDefaultBlockSize, kDefaultBloomFilterSize,
                          kDefaultLookback, kDefaultDupeFraction);
}

template <unsigned BlockSize>
void block_size(::benchmark::State& state) {
  run_segmenter_benchmark(state, kDefaultGranularity, kDefaultWindowSize,
                          BlockSize, kDefaultBloomFilterSize, kDefaultLookback,
                          kDefaultDupeFraction);
}

template <unsigned BloomFilterSize>
void bloom_filter_size(::benchmark::State& state) {
  run_segmenter_benchmark(state, kDefaultGranularity, kDefaultWindowSize,
                          kDefaultBlockSize, BloomFilterSize, kDefaultLookback,
                          kDefaultDupeFraction);
}

template <unsigned Lookback>
void lookback(::benchmark::State& state) {
  run_segmenter_benchmark(state, kDefaultGranularity, kDefaultWindowSize,
                          kDefaultBlockSize, kDefaultBloomFilterSize, Lookback,
                          kDefaultDupeFraction);
}

template <unsigned DupeFraction>
void dupe_fraction(::benchmark::State& state) {
  run_segmenter_benchmark(state, kDefaultGranularity, kDefaultWindowSize,
                          kDefaultBlockSize, kDefaultBloomFilterSize,
                          kDefaultLookback, 0.01 * DupeFraction);
}

} // namespace

BENCHMARK(granularity<1>)->Unit(benchmark::kMillisecond);
BENCHMARK(granularity<2>)->Unit(benchmark::kMillisecond);
BENCHMARK(granularity<3>)->Unit(benchmark::kMillisecond);
BENCHMARK(granularity<4>)->Unit(benchmark::kMillisecond);
BENCHMARK(granularity<5>)->Unit(benchmark::kMillisecond);
BENCHMARK(granularity<6>)->Unit(benchmark::kMillisecond);

BENCHMARK(window_size<8>)->Unit(benchmark::kMillisecond);
BENCHMARK(window_size<10>)->Unit(benchmark::kMillisecond);
BENCHMARK(window_size<12>)->Unit(benchmark::kMillisecond);
BENCHMARK(window_size<14>)->Unit(benchmark::kMillisecond);
BENCHMARK(window_size<16>)->Unit(benchmark::kMillisecond);

BENCHMARK(block_size<18>)->Unit(benchmark::kMillisecond);
BENCHMARK(block_size<20>)->Unit(benchmark::kMillisecond);
BENCHMARK(block_size<22>)->Unit(benchmark::kMillisecond);
BENCHMARK(block_size<24>)->Unit(benchmark::kMillisecond);
BENCHMARK(block_size<26>)->Unit(benchmark::kMillisecond);

BENCHMARK(bloom_filter_size<1>)->Unit(benchmark::kMillisecond);
BENCHMARK(bloom_filter_size<2>)->Unit(benchmark::kMillisecond);
BENCHMARK(bloom_filter_size<3>)->Unit(benchmark::kMillisecond);
BENCHMARK(bloom_filter_size<4>)->Unit(benchmark::kMillisecond);
BENCHMARK(bloom_filter_size<5>)->Unit(benchmark::kMillisecond);
BENCHMARK(bloom_filter_size<6>)->Unit(benchmark::kMillisecond);

BENCHMARK(lookback<1>)->Unit(benchmark::kMillisecond);
BENCHMARK(lookback<2>)->Unit(benchmark::kMillisecond);
BENCHMARK(lookback<4>)->Unit(benchmark::kMillisecond);
BENCHMARK(lookback<8>)->Unit(benchmark::kMillisecond);
BENCHMARK(lookback<16>)->Unit(benchmark::kMillisecond);

BENCHMARK(dupe_fraction<0>)->Unit(benchmark::kMillisecond);
BENCHMARK(dupe_fraction<20>)->Unit(benchmark::kMillisecond);
BENCHMARK(dupe_fraction<40>)->Unit(benchmark::kMillisecond);
BENCHMARK(dupe_fraction<60>)->Unit(benchmark::kMillisecond);
BENCHMARK(dupe_fraction<80>)->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();
