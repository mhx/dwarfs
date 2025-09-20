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

#include <algorithm>
#include <array>
#include <random>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <range/v3/view/enumerate.hpp>

#include <dwarfs/config.h>
#include <dwarfs/error.h>
#include <dwarfs/reader/block_cache_options.h>
#include <dwarfs/reader/cache_tidy_config.h>
#include <dwarfs/reader/filesystem_options.h>
#include <dwarfs/reader/filesystem_v2.h>
#include <dwarfs/tool/main_adapter.h>
#include <dwarfs_tool_main.h>

#include "mmap_mock.h"
#include "test_helpers.h"
#include "test_logger.h"

using namespace dwarfs;

class options_test
    : public ::testing::TestWithParam<reader::block_cache_options> {
  DWARFS_SLOW_FIXTURE
};

TEST_P(options_test, cache_stress) {
  static constexpr size_t num_threads{8};
  static constexpr size_t num_read_reqs{1024};

  auto const& cache_opts = GetParam();

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

  file_view mm;

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
    EXPECT_EQ(0, tool::main_adapter(tool::mkdwarfs_main)(args, iol.get()));

    mm = test::make_mock_file_view(iol.out());
  }

  test::test_logger lgr(logger::TRACE);
  reader::filesystem_options opts{
      .block_cache = cache_opts,
  };
  reader::filesystem_v2 fs(lgr, *os, mm, opts);

  EXPECT_NO_THROW(fs.set_cache_tidy_config(
      {.strategy = reader::cache_tidy_strategy::NONE}));

  EXPECT_THAT(
      [&] {
        fs.set_cache_tidy_config({
            .strategy = reader::cache_tidy_strategy::BLOCK_SWAPPED_OUT,
            .interval = std::chrono::seconds::zero(),
        });
      },
      ::testing::ThrowsMessage<dwarfs::runtime_error>(
          ::testing::HasSubstr("tidy interval is zero")));

  fs.set_cache_tidy_config({
      .strategy = reader::cache_tidy_strategy::BLOCK_SWAPPED_OUT,
  });

  fs.set_num_workers(cache_opts.num_workers);

  fs.set_cache_tidy_config({
      .strategy = reader::cache_tidy_strategy::EXPIRY_TIME,
      .interval = std::chrono::milliseconds(1),
      .expiry_time = std::chrono::milliseconds(2),
  });

  std::vector<reader::inode_view> inodes;

  fs.walk([&](auto e) { inodes.push_back(e.inode()); });

  std::uniform_int_distribution<size_t> inode_dist(0, inodes.size() - 1);

  struct read_request {
    reader::inode_view inode;
    size_t offset;
    size_t size;
  };

  std::vector<std::vector<read_request>> data(num_threads);
  std::mt19937_64 rng{42};

  for (auto& reqs : data) {
    while (reqs.size() < num_read_reqs) {
      auto iv = inodes[inode_dist(rng)];
      auto stat = fs.getattr(iv);
      if (stat.is_regular_file()) {
        size_t offset = rng() % stat.size();
        size_t size = rng() % (stat.size() - offset);
        reqs.push_back({iv, offset, size});

        while (reqs.size() < num_read_reqs &&
               offset + size < static_cast<size_t>(stat.size() / 2)) {
          offset += rng() % (stat.size() - (offset + size));
          size = rng() % (stat.size() - offset);
          reqs.push_back({iv, offset, size});
        }
      }
    }
  }

  std::vector<std::thread> threads;
  std::vector<size_t> success(num_threads);

  for (auto const& [i, reqs] : ranges::views::enumerate(data)) {
    auto& succ = success[i];
    // TODO: preqs is a workaround for older Clang versions
    threads.emplace_back([&, preqs = &reqs] {
      for (auto const& req : *preqs) {
        auto fh = fs.open(req.inode);
        std::error_code ec;
        auto ranges = fs.readv(fh, req.size, req.offset, ec);
        if (ec) {
          std::cerr << "read failed: " << ec.message() << std::endl;
          std::terminate();
        }
        try {
          for (auto& b : ranges) {
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

  for (auto const& [i, reqs] : ranges::views::enumerate(success)) {
    EXPECT_EQ(reqs, num_read_reqs) << i;
  }
}

namespace {

using reader::block_cache_options;

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
                        .disable_block_integrity_check = true},
};

} // namespace

INSTANTIATE_TEST_SUITE_P(block_cache, options_test,
                         ::testing::ValuesIn(cache_options));
