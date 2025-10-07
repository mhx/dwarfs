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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <array>
#include <numeric>
#include <tuple>
#include <vector>

#include <folly/portability/Stdlib.h>

#include <dwarfs/binary_literals.h>
#include <dwarfs/error.h>
#include <dwarfs/util.h>

#include <dwarfs/reader/internal/offset_cache.h>

using namespace dwarfs;
using namespace dwarfs::binary_literals;

#ifdef GTEST_NO_U8STRING
#define EXPECT_EQ_U8STR(a, b) EXPECT_TRUE((a) == (b))
#else
#define EXPECT_EQ_U8STR(a, b) EXPECT_EQ(a, b)
#endif

TEST(utils, utf8_display_width) {
  EXPECT_EQ_U8STR(0, utf8_display_width(""));
  EXPECT_EQ_U8STR(1, utf8_display_width(u8string_to_string(u8"a")));
  EXPECT_EQ_U8STR(5, utf8_display_width(u8string_to_string(u8"abcde")));
  EXPECT_EQ_U8STR(2, utf8_display_width(u8string_to_string(u8"你")));
  EXPECT_EQ_U8STR(4, utf8_display_width(u8string_to_string(u8"我你")));
  EXPECT_EQ_U8STR(6, utf8_display_width(u8string_to_string(u8"我爱你")));
  EXPECT_EQ_U8STR(5, utf8_display_width(u8string_to_string(u8"☀️ Sun")));
  EXPECT_EQ_U8STR(2, utf8_display_width(u8string_to_string(u8"⚽️")));
  EXPECT_EQ_U8STR(5, utf8_display_width(u8string_to_string(u8"مرحبًا")));
  EXPECT_EQ_U8STR(50,
                  utf8_display_width(u8string_to_string(
                      u8"unicode/我爱你/☀️ Sun/Γειά σας/مرحبًا/⚽️/Карибського")));
}

TEST(utils, uft8_truncate) {
  auto u8trunc = [](std::u8string str, size_t len) {
    auto tmp = u8string_to_string(str);
    utf8_truncate(tmp, len);
    return string_to_u8string(tmp);
  };

  // -----------------123456789012345--
  auto const str = u8"我爱你/مرحبًا/⚽️";

  EXPECT_EQ_U8STR(str, u8trunc(str, 15));
  // ----------123456789012345--
  EXPECT_EQ_U8STR(u8"我爱你/مرحبًا/", u8trunc(str, 14));
  EXPECT_EQ_U8STR(u8"我爱你/مرحبًا/", u8trunc(str, 13));
  EXPECT_EQ_U8STR(u8"我爱你/مرحبًا", u8trunc(str, 12));
  EXPECT_EQ_U8STR(u8"我爱你/مرحبً", u8trunc(str, 11));
  EXPECT_EQ_U8STR(u8"我爱你/مرح", u8trunc(str, 10));
  EXPECT_EQ_U8STR(u8"我爱你/مر", u8trunc(str, 9));
  EXPECT_EQ_U8STR(u8"我爱你/م", u8trunc(str, 8));
  EXPECT_EQ_U8STR(u8"我爱你/", u8trunc(str, 7));
  EXPECT_EQ_U8STR(u8"我爱你", u8trunc(str, 6));
  EXPECT_EQ_U8STR(u8"我爱", u8trunc(str, 5));
  EXPECT_EQ_U8STR(u8"我爱", u8trunc(str, 4));
  EXPECT_EQ_U8STR(u8"我", u8trunc(str, 3));
  EXPECT_EQ_U8STR(u8"我", u8trunc(str, 2));
  EXPECT_EQ_U8STR(u8"", u8trunc(str, 1));
}

TEST(utils, shorten_path_ascii) {
  std::string const orig =
      "/foo/bar/home/bla/mnt/doc/html/boost_asio/reference/"
      "async_result_lt__basic_yield_context_lt__Executor__gt__comma__Signature_"
      "_gt_/handler_type.html";
  size_t const max_max_len = orig.size() + 10;

  for (size_t max_len = 0; max_len < max_max_len; ++max_len) {
    std::string path = orig;

    shorten_path_string(path, '/', max_len);

    EXPECT_LE(path.size(), max_len);

    if (max_len >= orig.size()) {
      EXPECT_EQ(path, orig) << "[" << max_len << "]";
    } else if (max_len >= 3) {
      EXPECT_TRUE(path.starts_with("...")) << "[" << max_len << "] " << path;
      if (path.size() > 3) {
        EXPECT_TRUE(path.starts_with(".../")) << "[" << max_len << "] " << path;
      }
    }
  }

  {
    static std::vector<std::string> const expected{
        "",
        "",
        "",
        "...",
        "...",
        "...",
        ".../ee",
        ".../ee",
        ".../ee",
        ".../dd/ee",
        ".../dd/ee",
        ".../dd/ee",
        ".../cc/dd/ee",
        ".../cc/dd/ee",
        ".../cc/dd/ee",
        "/aa/bb/cc/dd/ee",
        "/aa/bb/cc/dd/ee",
        "/aa/bb/cc/dd/ee",
    };

    for (size_t max_len = 0; max_len < expected.size(); ++max_len) {
      std::string path = "/aa/bb/cc/dd/ee";
      shorten_path_string(path, '/', max_len);
      EXPECT_EQ(expected[max_len], path) << "[" << max_len << "]" << path;
    }
  }
}

TEST(utils, shorten_path_utf8) {
  std::u8string const orig_u8 =
      u8"/unicode/我爱你/☀️ Sun/Γειά σας/مرحبًا/⚽️/Карибського";
  std::string const orig = u8string_to_string(orig_u8);

  size_t const orig_len = utf8_display_width(orig);
  size_t const max_max_len = orig_len + 10;

  for (size_t max_len = 0; max_len < max_max_len; ++max_len) {
    std::string path = orig;

    shorten_path_string(path, '/', max_len);

    EXPECT_LE(utf8_display_width(path), max_len) << path;

    if (max_len >= orig_len) {
      EXPECT_EQ(path, orig) << "[" << max_len << "]";
    } else if (max_len >= 3) {
      EXPECT_TRUE(path.starts_with("...")) << "[" << max_len << "] " << path;
      if (path.size() > 3) {
        EXPECT_TRUE(path.starts_with(".../")) << "[" << max_len << "] " << path;
      }
    }
  }

  {
    static std::vector<std::u8string> const expected{
        u8"",
        u8"",
        u8"",
        u8"...",
        u8"...",
        u8"...",
        u8".../го",
        u8".../го",
        u8".../го",
        u8".../مر/го",
        u8".../مر/го",
        u8".../مر/го",
        u8".../Γε/مر/го",
        u8".../Γε/مر/го",
        u8".../Γε/مر/го",
        u8".../Γε/مر/го",
        u8"/我/☀️⚽️/Γε/مر/го",
        u8"/我/☀️⚽️/Γε/مر/го",
        u8"/我/☀️⚽️/Γε/مر/го",
    };

    std::u8string const path_u8 = u8"/我/☀️⚽️/Γε/مر/го";

    for (size_t max_len = 0; max_len < expected.size(); ++max_len) {
      std::string path = u8string_to_string(path_u8);
      shorten_path_string(path, '/', max_len);
      EXPECT_EQ_U8STR(u8string_to_string(expected[max_len]), path);
    }
  }
}

namespace {

using cache_type =
    reader::internal::basic_offset_cache<uint32_t, uint32_t, uint32_t, 4, 4>;
constexpr std::array<cache_type::file_offset_type, 32> const test_chunks{
    3, 15, 13, 1,  11, 6,  9,  15, 1,  16, 1,  13, 11, 16, 10, 14,
    4, 14, 4,  16, 8,  12, 16, 2,  16, 10, 15, 15, 2,  15, 5,  8,
};
constexpr cache_type::inode_type const test_inode = 42;
constexpr size_t const total_size =
    std::accumulate(test_chunks.begin(), test_chunks.end(), 0);

std::tuple<cache_type::chunk_index_type, cache_type::file_offset_type, size_t>
find_file_position(cache_type::inode_type const inode,
                   std::span<cache_type::file_offset_type const> chunks,
                   cache_type::file_offset_type file_offset,
                   cache_type* cache = nullptr) {
  cache_type::value_type ent;

  if (cache) {
    ent = cache->find(inode, chunks.size());

    if (!ent) {
      throw std::runtime_error("find() did not return an object");
    }
  }

  auto upd = cache_type::updater();
  auto it = chunks.begin();
  auto end = chunks.end();
  cache_type::chunk_index_type chunk_index = 0;
  cache_type::file_offset_type chunk_offset = 0;

  if (ent) {
    std::tie(chunk_index, chunk_offset) = ent->find(file_offset, upd);
    std::advance(it, chunk_index);
  }

  auto remaining_offset = file_offset - chunk_offset;
  size_t num_lookups = 0;

  while (it < end) {
    ++num_lookups;
    auto chunk_size = *it;

    if (remaining_offset < chunk_size) {
      break;
    }

    remaining_offset -= chunk_size;
    chunk_offset += chunk_size;
    ++it;

    upd.add_offset(++chunk_index, chunk_offset);
  }

  if (cache) {
    ent->update(upd, chunk_index, chunk_offset, *it);
    cache->set(inode, ent);
  }

  return {chunk_index, remaining_offset, num_lookups};
}

} // namespace

TEST(utils, offset_cache_basic) {
  cache_type cache(4);

  size_t total_ref_lookups = 0;
  size_t total_test_lookups = 0;

  for (cache_type::file_offset_type offset = 0; offset < total_size; ++offset) {
    auto [ref_ix, ref_off, ref_lookups] =
        find_file_position(test_inode, test_chunks, offset);

    auto [test_ix, test_off, test_lookups] =
        find_file_position(test_inode, test_chunks, offset, &cache);

    auto ref_offset = std::accumulate(test_chunks.begin(),
                                      test_chunks.begin() + ref_ix, ref_off);

    EXPECT_EQ(offset, ref_offset);

    EXPECT_EQ(ref_ix + 1, ref_lookups);
    EXPECT_LE(test_lookups, 2);

    EXPECT_EQ(ref_ix, test_ix);
    EXPECT_EQ(ref_off, test_off);

    total_ref_lookups += ref_lookups;
    total_test_lookups += test_lookups;
  }

  EXPECT_GT(total_test_lookups, 0);
  EXPECT_LT(total_test_lookups, total_ref_lookups);

  for (cache_type::file_offset_type offset = total_size; offset-- > 0;) {
    auto [ref_ix, ref_off, ref_lookups] =
        find_file_position(test_inode, test_chunks, offset);

    auto [test_ix, test_off, test_lookups] =
        find_file_position(test_inode, test_chunks, offset, &cache);

    auto ref_offset = std::accumulate(test_chunks.begin(),
                                      test_chunks.begin() + ref_ix, ref_off);

    EXPECT_EQ(offset, ref_offset);

    EXPECT_EQ(ref_ix + 1, ref_lookups);
    EXPECT_LE(test_lookups, 5);

    EXPECT_EQ(ref_ix, test_ix);
    EXPECT_EQ(ref_off, test_off);

    total_ref_lookups += ref_lookups;
    total_test_lookups += test_lookups;
  }
}

TEST(utils, offset_cache_prefill) {
  cache_type prefilled_cache(4);

  auto [prefill_ix, prefill_off, prefill_lookups] = find_file_position(
      test_inode, test_chunks, total_size - 1, &prefilled_cache);

  EXPECT_EQ(test_chunks.size(), prefill_lookups);
  EXPECT_EQ(test_chunks.size() - 1, prefill_ix);
  EXPECT_EQ(test_chunks.back() - 1, prefill_off);
}

TEST(utils, parse_time_with_unit) {
  using namespace std::chrono_literals;
  EXPECT_EQ(3ms, parse_time_with_unit("3ms"));
  EXPECT_EQ(4s, parse_time_with_unit("4s"));
  EXPECT_EQ(5s, parse_time_with_unit("5"));
  EXPECT_EQ(6min, parse_time_with_unit("6m"));
  EXPECT_EQ(7h, parse_time_with_unit("7h"));
  EXPECT_THROW(parse_time_with_unit("8y"), dwarfs::runtime_error);
  EXPECT_THROW(parse_time_with_unit("8su"), dwarfs::runtime_error);
  EXPECT_THROW(parse_time_with_unit("8mss"), dwarfs::runtime_error);
  EXPECT_THROW(parse_time_with_unit("ms"), dwarfs::runtime_error);
}

TEST(utils, parse_size_with_unit) {
  EXPECT_EQ(static_cast<file_size_t>(2), parse_size_with_unit("2"));
  EXPECT_EQ(static_cast<file_size_t>(3_KiB), parse_size_with_unit("3k"));
  EXPECT_EQ(static_cast<file_size_t>(4_MiB), parse_size_with_unit("4m"));
  EXPECT_EQ(static_cast<file_size_t>(5_GiB), parse_size_with_unit("5g"));
  EXPECT_EQ(static_cast<file_size_t>(6_TiB), parse_size_with_unit("6t"));
  EXPECT_EQ(static_cast<file_size_t>(1001_KiB), parse_size_with_unit("1001K"));
  EXPECT_EQ(static_cast<file_size_t>(1002_MiB), parse_size_with_unit("1002M"));
  EXPECT_EQ(static_cast<file_size_t>(1003_GiB), parse_size_with_unit("1003G"));
  EXPECT_EQ(static_cast<file_size_t>(1004_TiB), parse_size_with_unit("1004T"));
  EXPECT_THROW(parse_size_with_unit("7y"), dwarfs::runtime_error);
  EXPECT_THROW(parse_size_with_unit("7tb"), dwarfs::runtime_error);
  EXPECT_THROW(parse_size_with_unit("asd"), dwarfs::runtime_error);
}

TEST(utils, parse_time_point) {
  using namespace std::chrono_literals;
  using std::chrono::sys_days;

  EXPECT_EQ(sys_days{2020y / 1 / 1}, parse_time_point("2020-01-01"));
  EXPECT_EQ(sys_days{2020y / 1 / 1}, parse_time_point("2020-01-01T"));
  EXPECT_EQ(sys_days{2020y / 1 / 1}, parse_time_point("2020-01-01 00:00:00"));
  EXPECT_EQ(sys_days{2020y / 1 / 1}, parse_time_point("2020-01-01T00:00:00"));
  EXPECT_EQ(sys_days{2020y / 1 / 1}, parse_time_point("2020-01-01 00:00"));
  EXPECT_EQ(sys_days{2020y / 1 / 1}, parse_time_point("2020-01-01T00:00"));
  EXPECT_EQ(sys_days{2020y / 1 / 1}, parse_time_point("20200101T000000"));
  EXPECT_EQ(sys_days{2020y / 1 / 1}, parse_time_point("20200101T0000"));
  EXPECT_EQ(sys_days{2020y / 1 / 1}, parse_time_point("20200101T"));
  EXPECT_EQ(sys_days{2020y / 1 / 1} + 1h + 2min + 3s,
            parse_time_point("2020-01-01 01:02:03"));
  EXPECT_EQ(sys_days{2020y / 1 / 1} + 1h + 2min,
            parse_time_point("2020-01-01 01:02"));
  EXPECT_EQ(sys_days{2020y / 1 / 1} + 1h + 2min + 3s + 123ms,
            parse_time_point("2020-01-01 01:02:03.123"));
  EXPECT_EQ(sys_days{2020y / 1 / 1} + 1h + 2min + 3s + 123ms,
            parse_time_point("20200101T010203.123"));

  EXPECT_THAT([] { parse_time_point("InVaLiD"); },
              ::testing::ThrowsMessage<dwarfs::runtime_error>(
                  ::testing::HasSubstr("cannot parse time point")));
  EXPECT_THAT([] { parse_time_point("2020-01-01 01:02x"); },
              ::testing::ThrowsMessage<dwarfs::runtime_error>(
                  ::testing::HasSubstr("cannot parse time point")));
}

TEST(utils, getenv_is_enabled) {
  static char const* const test_var = "_DWARFS_THIS_IS_A_TEST_";

  EXPECT_EQ(0, unsetenv("_DWARFS_THIS_IS_A_TEST_"));
  EXPECT_FALSE(getenv_is_enabled(test_var));

  EXPECT_EQ(0, setenv(test_var, "0", 1));
  EXPECT_FALSE(getenv_is_enabled(test_var));

  EXPECT_EQ(0, setenv(test_var, "1", 1));
  EXPECT_TRUE(getenv_is_enabled(test_var));

  EXPECT_EQ(0, setenv(test_var, "false", 1));
  EXPECT_FALSE(getenv_is_enabled(test_var));

  EXPECT_EQ(0, setenv(test_var, "true", 1));
  EXPECT_TRUE(getenv_is_enabled(test_var));

  EXPECT_EQ(0, setenv(test_var, "off", 1));
  EXPECT_FALSE(getenv_is_enabled(test_var));

  EXPECT_EQ(0, setenv(test_var, "on", 1));
  EXPECT_TRUE(getenv_is_enabled(test_var));

  EXPECT_EQ(0, setenv(test_var, "ThisAintBool", 1));
  EXPECT_FALSE(getenv_is_enabled(test_var));

  EXPECT_EQ(0, unsetenv(test_var));
  EXPECT_FALSE(getenv_is_enabled(test_var));
}

TEST(utils, size_with_unit) {
  EXPECT_EQ("0 B", size_with_unit(0));
  EXPECT_EQ("1023 B", size_with_unit(1023));
  EXPECT_EQ("1 KiB", size_with_unit(1024));
  EXPECT_EQ("1.5 KiB", size_with_unit(1536));
  EXPECT_EQ("97.66 KiB", size_with_unit(100'000));
  EXPECT_EQ("256 KiB", size_with_unit(256_KiB));
  EXPECT_EQ("1024 KiB", size_with_unit(1_MiB - 1));
  EXPECT_EQ("1 MiB", size_with_unit(1_MiB));
  EXPECT_EQ("1024 MiB", size_with_unit(1_GiB - 1));
  EXPECT_EQ("1 GiB", size_with_unit(1_GiB));
  EXPECT_EQ("1024 GiB", size_with_unit(1_TiB - 1));
  EXPECT_EQ("1 TiB", size_with_unit(1_TiB));
  EXPECT_EQ("1024 TiB", size_with_unit(1_TiB * 1024 - 1));
  EXPECT_EQ("1 PiB", size_with_unit(1_TiB * 1024));
}

TEST(utils, time_with_unit) {
  using namespace std::chrono_literals;
  EXPECT_EQ("0s", time_with_unit(0ms));
  EXPECT_EQ("999ms", time_with_unit(999ms));
  EXPECT_EQ("1s", time_with_unit(1000ms));
  EXPECT_EQ("1.5s", time_with_unit(1500ms));
  EXPECT_EQ("59s", time_with_unit(59s));
  EXPECT_EQ("1m", time_with_unit(60s));
  EXPECT_EQ("1.017m", time_with_unit(61s));
  EXPECT_EQ("1.75m", time_with_unit(105s));
  EXPECT_EQ("12.5us", time_with_unit(12500ns));
}

TEST(utils, ratio_to_string) {
  EXPECT_EQ("0x", ratio_to_string(0, 1));
  EXPECT_EQ("1x", ratio_to_string(1, 1));
  EXPECT_EQ("1.5x", ratio_to_string(3, 2));
  EXPECT_EQ("10.7x", ratio_to_string(10.744, 1));
  EXPECT_EQ("11x", ratio_to_string(10.744, 1, 2));
  EXPECT_EQ("10.74x", ratio_to_string(10.744, 1, 4));
  EXPECT_EQ("99.9%", ratio_to_string(999, 1000));
  EXPECT_EQ("0.1%", ratio_to_string(1, 1000));
  EXPECT_EQ("999ppm", ratio_to_string(999, 1'000'000));
  EXPECT_EQ("1ppm", ratio_to_string(1, 1'000'000));
  EXPECT_EQ("1.5ppm", ratio_to_string(3, 2'000'000));
  EXPECT_EQ("10.7ppm", ratio_to_string(10'744, 1'000'000'000));
  EXPECT_EQ("999ppb", ratio_to_string(999, 1'000'000'000));
  EXPECT_EQ("1ppb", ratio_to_string(1, 1'000'000'000));
  EXPECT_EQ("1.78e-12x", ratio_to_string(1.7777, 1'000'000'000'000));
}
