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
#include <numeric>
#include <optional>
#include <random>
#include <span>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <folly/container/Enumerate.h>

#include "dwarfs/block_range.h"
#include "dwarfs/cached_block.h"
#include "dwarfs/error.h"
#include "dwarfs/filesystem_v2.h"
#include "dwarfs_tool_main.h"

#include "mmap_mock.h"
#include "test_helpers.h"
#include "test_logger.h"

using namespace dwarfs;

namespace {

class mock_cached_block : public cached_block {
 public:
  mock_cached_block() = default;
  mock_cached_block(std::span<uint8_t const> span)
      : span_{span} {}

  size_t range_end() const override { return span_ ? span_->size() : 0; }
  const uint8_t* data() const override {
    return span_ ? span_->data() : nullptr;
  }
  void decompress_until(size_t) override {}
  size_t uncompressed_size() const override { return 0; }
  void touch() override {}
  bool last_used_before(std::chrono::steady_clock::time_point) const override {
    return false;
  }
  bool any_pages_swapped_out(std::vector<uint8_t>&) const override {
    return false;
  }

 private:
  std::optional<std::span<uint8_t const>> span_;
};

} // namespace

TEST(block_range, uncompressed) {
  std::vector<uint8_t> data(100);
  std::iota(data.begin(), data.end(), 0);

  {
    block_range range{data.data(), 0, data.size()};
    EXPECT_EQ(range.data(), data.data());
    EXPECT_EQ(range.size(), 100);
    EXPECT_TRUE(std::equal(range.begin(), range.end(), data.begin()));
  }

  {
    block_range range{data.data(), 10, 20};
    EXPECT_EQ(range.size(), 20);
    EXPECT_TRUE(std::equal(range.begin(), range.end(), data.begin() + 10));
  }

  EXPECT_THAT([] { block_range range(nullptr, 0, 0); },
              ::testing::ThrowsMessage<dwarfs::runtime_error>(
                  ::testing::HasSubstr("block_range: block data is null")));
}

TEST(block_range, compressed) {
  std::vector<uint8_t> data(100);
  std::iota(data.begin(), data.end(), 0);

  {
    auto block = std::make_shared<mock_cached_block>(data);
    block_range range{block, 0, data.size()};
    EXPECT_EQ(range.data(), data.data());
    EXPECT_EQ(range.size(), 100);
    EXPECT_TRUE(std::equal(range.begin(), range.end(), data.begin()));
  }

  {
    auto block = std::make_shared<mock_cached_block>(data);
    block_range range{block, 10, 20};
    EXPECT_EQ(range.size(), 20);
    EXPECT_TRUE(std::equal(range.begin(), range.end(), data.begin() + 10));
  }

  EXPECT_THAT(
      [] {
        auto block = std::make_shared<mock_cached_block>();
        block_range range(block, 0, 0);
      },
      ::testing::ThrowsMessage<dwarfs::runtime_error>(
          ::testing::HasSubstr("block_range: block data is null")));

  EXPECT_THAT(
      [&] {
        auto block = std::make_shared<mock_cached_block>(data);
        block_range range(block, 100, 1);
      },
      ::testing::ThrowsMessage<dwarfs::runtime_error>(
          ::testing::HasSubstr("block_range: size out of range (101 > 100)")));
}

class options_test : public ::testing::TestWithParam<block_cache_options> {};

TEST_P(options_test, cache_stress) {
  static constexpr size_t num_threads{8};
  static constexpr size_t num_read_reqs{1024};

  auto const& cache_opts = GetParam();

  std::mt19937_64 rng{42};
  auto os = std::make_shared<test::os_access_mock>();

  {
    static constexpr size_t const num_files{256};
    static constexpr size_t const avg_size{5000};
    static constexpr size_t const max_size{16 * avg_size};
    std::mt19937_64 rng{42};
    std::exponential_distribution<> size_dist{1.0 / avg_size};

    os->add("", {1, 040755, 1, 0, 0, 10, 42, 0, 0, 0});

    for (size_t x = 0; x < num_files; ++x) {
      auto size = std::min(max_size, static_cast<size_t>(size_dist(rng)));
      os->add_file(std::to_string(x),
                   test::create_random_string(size, 32, 127, rng));
    }
  }

  std::shared_ptr<mmif> mm;

  {
    auto fa = std::make_shared<test::test_file_access>();
    test::test_iolayer iol{os, fa};

#if defined(DWARFS_HAVE_LIBBROTLI)
    std::string compression{"brotli:quality=0"};
#elif defined(DWARFS_HAVE_LIBLZMA)
    std::string compression{"lzma:level=0"};
#else
    std::string compression{"zstd:level=5"};
#endif

    std::vector<std::string> args{"mkdwarfs", "-i",   "/",  "-o",       "-",
                                  "-l3",      "-S16", "-C", compression};
    EXPECT_EQ(0, mkdwarfs_main(args, iol.get()));

    mm = std::make_shared<test::mmap_mock>(iol.out());
  }

  test::test_logger lgr(logger::TRACE);
  filesystem_options opts{
      .block_cache = cache_opts,
  };
  filesystem_v2 fs(lgr, *os, mm, opts);

  EXPECT_NO_THROW(
      fs.set_cache_tidy_config({.strategy = cache_tidy_strategy::NONE}));

  EXPECT_THAT(
      [&] {
        fs.set_cache_tidy_config({
            .strategy = cache_tidy_strategy::BLOCK_SWAPPED_OUT,
            .interval = std::chrono::seconds::zero(),
        });
      },
      ::testing::ThrowsMessage<dwarfs::runtime_error>(
          ::testing::HasSubstr("tidy interval is zero")));

  fs.set_cache_tidy_config({
      .strategy = cache_tidy_strategy::BLOCK_SWAPPED_OUT,
  });

  fs.set_num_workers(cache_opts.num_workers);

  fs.set_cache_tidy_config({
      .strategy = cache_tidy_strategy::EXPIRY_TIME,
      .interval = std::chrono::milliseconds(1),
      .expiry_time = std::chrono::milliseconds(2),
  });

  std::vector<inode_view> inodes;

  fs.walk([&](auto e) { inodes.push_back(e.inode()); });

  std::uniform_int_distribution<size_t> inode_dist(0, inodes.size() - 1);

  struct read_request {
    inode_view inode;
    size_t offset;
    size_t size;
  };

  std::vector<std::vector<read_request>> data(num_threads);

  for (auto& reqs : data) {
    while (reqs.size() < num_read_reqs) {
      auto iv = inodes[inode_dist(rng)];
      file_stat stat;
      EXPECT_EQ(0, fs.getattr(iv, &stat));
      if (stat.is_regular_file()) {
        auto offset = rng() % stat.size;
        auto size = rng() % (stat.size - offset);
        reqs.push_back({iv, offset, size});

        while (reqs.size() < num_read_reqs &&
               offset + size < static_cast<size_t>(stat.size / 2)) {
          offset += rng() % (stat.size - (offset + size));
          size = rng() % (stat.size - offset);
          reqs.push_back({iv, offset, size});
        }
      }
    }
  }

  std::vector<std::thread> threads;
  std::vector<size_t> success(num_threads);

  for (auto const& [i, reqs] : folly::enumerate(data)) {
    auto& succ = success[i];
    threads.emplace_back([&] {
      for (auto const& req : reqs) {
        auto fh = fs.open(req.inode);
        auto range = fs.readv(fh, req.size, req.offset);
        if (!range) {
          std::cerr << "read failed: " << range.error() << std::endl;
          std::terminate();
        }
        try {
          for (auto& b : range.value()) {
            b.get();
          }
        } catch (std::exception const& e) {
          std::cerr << "read failed: " << e.what() << std::endl;
          std::terminate();
        }
        ++succ;
      }
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  for (auto const& [i, reqs] : folly::enumerate(success)) {
    EXPECT_EQ(reqs, num_read_reqs) << i;
  }
}

namespace {

constexpr std::array const cache_options{
    block_cache_options{.max_bytes = 0, .num_workers = 0},
    block_cache_options{.max_bytes = 256 * 1024, .num_workers = 0},
    block_cache_options{.max_bytes = 256 * 1024, .num_workers = 1},
    block_cache_options{.max_bytes = 256 * 1024, .num_workers = 3},
    block_cache_options{.max_bytes = 256 * 1024, .num_workers = 7},
    block_cache_options{.max_bytes = 1024 * 1024, .num_workers = 5},
    block_cache_options{
        .max_bytes = 1024 * 1024, .num_workers = 5, .decompress_ratio = 0.1},
    block_cache_options{
        .max_bytes = 1024 * 1024, .num_workers = 5, .decompress_ratio = 0.5},
    block_cache_options{
        .max_bytes = 1024 * 1024, .num_workers = 5, .decompress_ratio = 0.9},
    block_cache_options{.max_bytes = 512 * 1024,
                        .num_workers = 4,
                        .mm_release = false,
                        .disable_block_integrity_check = true},
};

}

INSTANTIATE_TEST_SUITE_P(block_cache, options_test,
                         ::testing::ValuesIn(cache_options));
