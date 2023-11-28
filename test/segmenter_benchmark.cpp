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

#include <random>
#include <vector>

#include <folly/Benchmark.h>

#include "dwarfs/block_data.h"
#include "dwarfs/block_manager.h"
#include "dwarfs/chunkable.h"
#include "dwarfs/compression_constraints.h"
#include "dwarfs/progress.h"
#include "dwarfs/segmenter.h"

#include "loremipsum.h"
#include "test_logger.h"

namespace {

class bench_chunkable : public dwarfs::chunkable {
 public:
  bench_chunkable(std::vector<uint8_t> data)
      : data_{std::move(data)} {}

  dwarfs::file const* get_file() const override { return nullptr; }

  size_t size() const override { return data_.size(); }

  std::string description() const override { return std::string(); }

  std::span<uint8_t const> span() const override { return data_; }

  void
  add_chunk(size_t /*block*/, size_t /*offset*/, size_t /*size*/) override {}

  void release_until(size_t /*offset*/) override {}

 private:
  std::vector<uint8_t> data_;
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

void run_segmenter_test(unsigned iters, unsigned granularity,
                        unsigned window_size, unsigned block_size,
                        unsigned bloom_filter_size, unsigned lookback,
                        double dupe_fraction) {
  folly::BenchmarkSuspender suspender;

  dwarfs::segmenter::config cfg;
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

  for (unsigned i = 0; i < iters; ++i) {
    dwarfs::test::test_logger lgr;
    dwarfs::progress prog([](dwarfs::progress const&, bool) {}, 1000);
    auto blkmgr = std::make_shared<dwarfs::block_manager>();

    std::vector<std::shared_ptr<dwarfs::block_data>> written;

    dwarfs::segmenter seg(
        lgr, prog, blkmgr, cfg, cc, total_size,
        [&written, blkmgr](std::shared_ptr<dwarfs::block_data> blk,
                           auto logical_block_num) {
          auto physical_block_num = written.size();
          written.push_back(blk);
          blkmgr->set_written_block(logical_block_num, physical_block_num, 0);
        });

    suspender.dismiss();

    seg.add_chunkable(bc);
    seg.finish();

    suspender.rehire();

    size_t segmented [[maybe_unused]]{0};

    for (auto const& blk : written) {
      segmented += blk->size();
    }

    // std::cerr << total_size << " -> " << segmented << fmt::format("
    // ({:.1f}%)", 100.0*segmented/total_size) << std::endl;
  }
}

constexpr unsigned const kDefaultGranularity{1};
constexpr unsigned const kDefaultWindowSize{12};
constexpr unsigned const kDefaultBlockSize{24};
constexpr unsigned const kDefaultBloomFilterSize{4};
constexpr unsigned const kDefaultLookback{1};
constexpr double const kDefaultDupeFraction{0.3};

void run_granularity(unsigned iters, unsigned granularity) {
  run_segmenter_test(iters, granularity, kDefaultWindowSize, kDefaultBlockSize,
                     kDefaultBloomFilterSize, kDefaultLookback,
                     kDefaultDupeFraction);
}

void run_window_size(unsigned iters, unsigned window_size) {
  run_segmenter_test(iters, kDefaultGranularity, window_size, kDefaultBlockSize,
                     kDefaultBloomFilterSize, kDefaultLookback,
                     kDefaultDupeFraction);
}

void run_block_size(unsigned iters, unsigned block_size) {
  run_segmenter_test(iters, kDefaultGranularity, kDefaultWindowSize, block_size,
                     kDefaultBloomFilterSize, kDefaultLookback,
                     kDefaultDupeFraction);
}

void run_bloom_filter_size(unsigned iters, unsigned bloom_filter_size) {
  run_segmenter_test(iters, kDefaultGranularity, kDefaultWindowSize,
                     kDefaultBlockSize, bloom_filter_size, kDefaultLookback,
                     kDefaultDupeFraction);
}

void run_lookback(unsigned iters, unsigned lookback) {
  run_segmenter_test(iters, kDefaultGranularity, kDefaultWindowSize,
                     kDefaultBlockSize, kDefaultBloomFilterSize, lookback,
                     kDefaultDupeFraction);
}

void run_dupe_fraction(unsigned iters, unsigned dupe_fraction) {
  run_segmenter_test(iters, kDefaultGranularity, kDefaultWindowSize,
                     kDefaultBlockSize, kDefaultBloomFilterSize,
                     kDefaultLookback, 0.01 * dupe_fraction);
}

} // namespace

BENCHMARK_DRAW_LINE();

BENCHMARK_PARAM(run_granularity, 1)
BENCHMARK_RELATIVE_PARAM(run_granularity, 2)
BENCHMARK_RELATIVE_PARAM(run_granularity, 3)
BENCHMARK_RELATIVE_PARAM(run_granularity, 4)
BENCHMARK_RELATIVE_PARAM(run_granularity, 5)
BENCHMARK_RELATIVE_PARAM(run_granularity, 6)

BENCHMARK_DRAW_LINE();

BENCHMARK_PARAM(run_window_size, 8)
BENCHMARK_RELATIVE_PARAM(run_window_size, 10)
BENCHMARK_RELATIVE_PARAM(run_window_size, 12)
BENCHMARK_RELATIVE_PARAM(run_window_size, 14)
BENCHMARK_RELATIVE_PARAM(run_window_size, 16)

BENCHMARK_DRAW_LINE();

BENCHMARK_PARAM(run_block_size, 18)
BENCHMARK_RELATIVE_PARAM(run_block_size, 20)
BENCHMARK_RELATIVE_PARAM(run_block_size, 22)
BENCHMARK_RELATIVE_PARAM(run_block_size, 24)
BENCHMARK_RELATIVE_PARAM(run_block_size, 26)

BENCHMARK_DRAW_LINE();

BENCHMARK_PARAM(run_bloom_filter_size, 1)
BENCHMARK_RELATIVE_PARAM(run_bloom_filter_size, 2)
BENCHMARK_RELATIVE_PARAM(run_bloom_filter_size, 3)
BENCHMARK_RELATIVE_PARAM(run_bloom_filter_size, 4)
BENCHMARK_RELATIVE_PARAM(run_bloom_filter_size, 5)
BENCHMARK_RELATIVE_PARAM(run_bloom_filter_size, 6)

BENCHMARK_DRAW_LINE();

BENCHMARK_PARAM(run_lookback, 1)
BENCHMARK_RELATIVE_PARAM(run_lookback, 2)
BENCHMARK_RELATIVE_PARAM(run_lookback, 4)
BENCHMARK_RELATIVE_PARAM(run_lookback, 8)
BENCHMARK_RELATIVE_PARAM(run_lookback, 16)

BENCHMARK_DRAW_LINE();

BENCHMARK_PARAM(run_dupe_fraction, 0)
BENCHMARK_RELATIVE_PARAM(run_dupe_fraction, 20)
BENCHMARK_RELATIVE_PARAM(run_dupe_fraction, 40)
BENCHMARK_RELATIVE_PARAM(run_dupe_fraction, 60)
BENCHMARK_RELATIVE_PARAM(run_dupe_fraction, 80)

BENCHMARK_DRAW_LINE();

int main(int argc, char* argv[]) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  folly::runBenchmarks();
}
