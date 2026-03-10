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
#include <csignal>
#include <numeric>
#include <tuple>
#include <vector>

#include <folly/portability/Stdlib.h>

#include <dwarfs/binary_literals.h>
#include <dwarfs/config.h>
#include <dwarfs/detail/scoped_env.h>
#include <dwarfs/error.h>
#include <dwarfs/util.h>

#include <dwarfs/reader/internal/offset_cache.h>

#include "test_helpers.h"

using namespace dwarfs;
using namespace dwarfs::binary_literals;

using testing::HasSubstr;
using testing::ThrowsMessage;

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
  EXPECT_EQ(3 * 86400s, parse_time_with_unit("3d"));
  EXPECT_EQ(86400s, parse_time_with_unit("day"));
  EXPECT_EQ(7 * 3600s, parse_time_with_unit("7h"));
  EXPECT_EQ(3600s, parse_time_with_unit("hour"));
  EXPECT_EQ(3ms, parse_time_with_unit("3ms"));
  EXPECT_EQ(4s, parse_time_with_unit("4s"));
  EXPECT_EQ(5s, parse_time_with_unit("5"));
  EXPECT_EQ(6min, parse_time_with_unit("6m"));
  EXPECT_EQ(7h, parse_time_with_unit("7h"));
  EXPECT_EQ(1ms, parse_time_with_unit("ms"));
  EXPECT_EQ(1ms, parse_time_with_unit("msec"));
  EXPECT_EQ(1us, parse_time_with_unit("us"));
  EXPECT_EQ(1us, parse_time_with_unit("usec"));
  EXPECT_EQ(1ns, parse_time_with_unit("ns"));
  EXPECT_EQ(1ns, parse_time_with_unit("nsec"));
  EXPECT_EQ(1ms, parse_time_with_unit("1 ms"));
  EXPECT_EQ(1ms, parse_time_with_unit("1 msec"));
  EXPECT_EQ(1us, parse_time_with_unit("1 us"));
  EXPECT_EQ(1us, parse_time_with_unit("1 usec"));
  EXPECT_EQ(1ns, parse_time_with_unit("1 ns"));
  EXPECT_EQ(1ns, parse_time_with_unit("1 nsec"));
  EXPECT_EQ(500ms, parse_time_with_unit("500ms"));
  EXPECT_EQ(500ms, parse_time_with_unit("500msec"));
  EXPECT_EQ(500us, parse_time_with_unit("500us"));
  EXPECT_EQ(500us, parse_time_with_unit("500usec"));
  EXPECT_EQ(500ns, parse_time_with_unit("500ns"));
  EXPECT_EQ(500ns, parse_time_with_unit("500nsec"));
}

TEST(utils, parse_time_with_unit_error) {
  EXPECT_THAT([] { parse_time_with_unit("18446744073709551616s"); },
              ThrowsMessage<dwarfs::runtime_error>(
                  HasSubstr("cannot parse time 18446744073709551616s: ")));
  EXPECT_THAT([] { parse_time_with_unit("8y"); },
              ThrowsMessage<dwarfs::runtime_error>(
                  HasSubstr("unsupported time suffix: y")));
  EXPECT_THAT([] { parse_time_with_unit("8su"); },
              ThrowsMessage<dwarfs::runtime_error>(
                  HasSubstr("unsupported time suffix: su")));
  EXPECT_THAT([] { parse_time_with_unit("8mss"); },
              ThrowsMessage<dwarfs::runtime_error>(
                  HasSubstr("unsupported time suffix: mss")));
  EXPECT_THAT([] { parse_time_with_unit("8hourd"); },
              ThrowsMessage<dwarfs::runtime_error>(
                  HasSubstr("unsupported time suffix: hourd")));
  EXPECT_THAT([] { parse_time_with_unit("8da"); },
              ThrowsMessage<dwarfs::runtime_error>(
                  HasSubstr("unsupported time suffix: da")));
  EXPECT_THAT([] { parse_time_with_unit("8um"); },
              ThrowsMessage<dwarfs::runtime_error>(
                  HasSubstr("unsupported time suffix: um")));
  EXPECT_THAT([] { parse_time_with_unit("8nm"); },
              ThrowsMessage<dwarfs::runtime_error>(
                  HasSubstr("unsupported time suffix: nm")));
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
              ThrowsMessage<dwarfs::runtime_error>(
                  HasSubstr("cannot parse time point")));
  EXPECT_THAT([] { parse_time_point("2020-01-01 01:02x"); },
              ThrowsMessage<dwarfs::runtime_error>(
                  HasSubstr("cannot parse time point")));
}

TEST(utils, getenv_is_enabled) {
  static constexpr auto test_var{"_DWARFS_UTILS_TEST_"};
  dwarfs::detail::scoped_env env;

  ASSERT_NO_THROW(env.unset(test_var));
  EXPECT_FALSE(getenv_is_enabled(test_var));

  ASSERT_NO_THROW(env.set(test_var, "0"));
  EXPECT_FALSE(getenv_is_enabled(test_var));

  ASSERT_NO_THROW(env.set(test_var, "1"));
  EXPECT_TRUE(getenv_is_enabled(test_var));

  ASSERT_NO_THROW(env.set(test_var, "false"));
  EXPECT_FALSE(getenv_is_enabled(test_var));

  ASSERT_NO_THROW(env.set(test_var, "true"));
  EXPECT_TRUE(getenv_is_enabled(test_var));

  ASSERT_NO_THROW(env.set(test_var, "ThisAintBool"));
  EXPECT_FALSE(getenv_is_enabled(test_var));

  ASSERT_NO_THROW(env.unset(test_var));
  EXPECT_FALSE(getenv_is_enabled(test_var));
}

TEST(utils, getenv_is_enabled_os) {
  static constexpr auto test_var{"_DWARFS_UTILS_TEST_"};
  test::os_access_mock os;

  EXPECT_FALSE(getenv_is_enabled(os, test_var));

  os.setenv(test_var, "0");
  EXPECT_FALSE(getenv_is_enabled(os, test_var));

  os.setenv(test_var, "1");
  EXPECT_TRUE(getenv_is_enabled(os, test_var));
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
  EXPECT_EQ("1m 1s", time_with_unit(61s));
  EXPECT_EQ("1m 45s", time_with_unit(105s));
  EXPECT_EQ("12.5us", time_with_unit(12500ns));
  EXPECT_EQ("1h 2m 3s", time_with_unit(1h + 2min + 3s));
  EXPECT_EQ("1m 1s", time_with_unit(1min + 1000ms));
  EXPECT_EQ("1m 1.5s", time_with_unit(1min + 1530ms));
  EXPECT_EQ("1m 1.5s", time_with_unit(1min + 1578ms));
  EXPECT_EQ("59m 1s", time_with_unit(59min + 1578ms));
  EXPECT_EQ("12h", time_with_unit(12h + 25s));
  EXPECT_EQ("12h", time_with_unit(12h + 40s));
  EXPECT_EQ("12h 1m", time_with_unit(12h + 70s));
  EXPECT_EQ("1d 1h 2m", time_with_unit(25h + 2min + 3s));
  EXPECT_EQ("1h", time_with_unit(1h + 400ms));
  EXPECT_EQ("1h 1s", time_with_unit(1h + 1200ms));
  EXPECT_EQ("59.99s", time_with_unit(59994ms));
  EXPECT_EQ("59.99s", time_with_unit(59995ms));
  EXPECT_EQ("1m", time_with_unit(60001ms));
  EXPECT_EQ("1m 59.9s", time_with_unit(119'995ms));
  EXPECT_EQ("2m", time_with_unit(120'005ms));
  EXPECT_EQ("2m 59.9s", time_with_unit(179'995ms));
  EXPECT_EQ("3m", time_with_unit(180'005ms));
  EXPECT_EQ("9m 59.9s", time_with_unit(599'995ms));
  EXPECT_EQ("10m", time_with_unit(600'005ms));
  EXPECT_EQ("10m", time_with_unit(600'995ms));
  EXPECT_EQ("10m 1s", time_with_unit(601'005ms));
  EXPECT_EQ("10m 59s", time_with_unit(659'440ms));
  EXPECT_EQ("11m", time_with_unit(660'005ms));
  EXPECT_EQ("11m", time_with_unit(660'990ms));
  EXPECT_EQ("11m 1s", time_with_unit(661'005ms));
  EXPECT_EQ("59m 59s", time_with_unit(3'599'990ms));
  EXPECT_EQ("1h", time_with_unit(3'600'005ms));
  EXPECT_EQ("1h", time_with_unit(3'600'990ms));
  EXPECT_EQ("1h 1s", time_with_unit(3'601'005ms));
  EXPECT_EQ("1h 59s", time_with_unit(3'659'990ms));
  EXPECT_EQ("1h 1m", time_with_unit(3'660'005ms));
  EXPECT_EQ("1h 1m", time_with_unit(3'660'990ms));
  EXPECT_EQ("1h 1m 1s", time_with_unit(3'661'005ms));
  EXPECT_EQ("1h 1m 59s", time_with_unit(3'719'440ms));
  EXPECT_EQ("1h 2m", time_with_unit(3'720'010ms));
  EXPECT_EQ("1h 2m", time_with_unit(3'720'790ms));
  EXPECT_EQ("1h 2m 1s", time_with_unit(3'721'020ms));
  EXPECT_EQ("1h 59m 59s", time_with_unit(7'199'690ms));
  EXPECT_EQ("2h", time_with_unit(7'200'010ms));
  EXPECT_EQ("2h", time_with_unit(7'200'990ms));
  EXPECT_EQ("2h 1s", time_with_unit(7'201'005ms));
  EXPECT_EQ("2h 59s", time_with_unit(7'259'940ms));
  EXPECT_EQ("9h 59m 59s", time_with_unit(35'999'990ms));
  EXPECT_EQ("10h", time_with_unit(36'000'005ms));
  EXPECT_EQ("10h", time_with_unit(36'059s));
  EXPECT_EQ("10h 1m", time_with_unit(36'061s));
  EXPECT_EQ("10h 59m", time_with_unit(39'599s));
  EXPECT_EQ("11h", time_with_unit(39'601s));
  EXPECT_EQ("11h", time_with_unit(39'659s));
  EXPECT_EQ("11h 1m", time_with_unit(39'661s));
  EXPECT_EQ("23h 59m", time_with_unit(86'399s));
  EXPECT_EQ("1d", time_with_unit(86'401s));
  EXPECT_EQ("1d", time_with_unit(86'459s));
  EXPECT_EQ("1d 1m", time_with_unit(86'461s));
  EXPECT_EQ("1d 59m", time_with_unit(89'999s));
  EXPECT_EQ("1d 1h", time_with_unit(90'001s));
  EXPECT_EQ("1d 1h", time_with_unit(90'059s));
  EXPECT_EQ("1d 1h 1m", time_with_unit(90'061s));
  EXPECT_EQ("1d 1h 59m", time_with_unit(93'599s));
  EXPECT_EQ("1d 2h", time_with_unit(93'601s));
  EXPECT_EQ("9d 23h 59m", time_with_unit(863'999s));
  EXPECT_EQ("10d", time_with_unit(864'001s));
  EXPECT_EQ("10d", time_with_unit(864'061s));
  EXPECT_EQ("10d", time_with_unit(865'799s));
  EXPECT_EQ("10d", time_with_unit(867'599s));
  EXPECT_EQ("10d 1h", time_with_unit(867'601s));
  EXPECT_EQ("10d 1h", time_with_unit(871'199s));
  EXPECT_EQ("10d 2h", time_with_unit(871'201s));
  EXPECT_EQ("99d 23h", time_with_unit(8'639'999s));
  EXPECT_EQ("100d", time_with_unit(8'640'001s));
  EXPECT_EQ("100d", time_with_unit(8'643'599s));
  EXPECT_EQ("100d 1h", time_with_unit(8'643'601s));
  EXPECT_EQ("999d 23h", time_with_unit(86'399'999s));
  EXPECT_EQ("1000d", time_with_unit(86'400'001s));
  EXPECT_EQ("1000d", time_with_unit(86'486'399s));
  EXPECT_EQ("1001d", time_with_unit(86'486'401s));
  EXPECT_EQ("10001d", time_with_unit(864'086'401s));
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

TEST(utils, basename) {
  using namespace std::string_view_literals;
  EXPECT_EQ(""sv, dwarfs::basename(""sv));
  EXPECT_EQ(""sv, dwarfs::basename("/"sv));
  EXPECT_EQ(""sv, dwarfs::basename("////"sv));
  EXPECT_EQ("foo"sv, dwarfs::basename("foo"sv));
  EXPECT_EQ("foo"sv, dwarfs::basename("/foo"sv));
  EXPECT_EQ("foo"sv, dwarfs::basename("///foo"sv));
  EXPECT_EQ("bar"sv, dwarfs::basename("/foo/bar"sv));
  EXPECT_EQ(""sv, dwarfs::basename("/foo/bar/"sv));
  EXPECT_EQ(""sv, dwarfs::basename("\\"sv));
  EXPECT_EQ(""sv, dwarfs::basename("\\\\\\\\"sv));
  EXPECT_EQ("foo"sv, dwarfs::basename("\\foo"sv));
  EXPECT_EQ("foo"sv, dwarfs::basename("\\\\foo"sv));
  EXPECT_EQ("bar"sv, dwarfs::basename("\\foo\\bar"sv));
  EXPECT_EQ(""sv, dwarfs::basename("\\foo\\bar\\"sv));
}

TEST(utils, scoped_env) {
  using namespace std::string_view_literals;

  {
    dwarfs::detail::scoped_env env;
    EXPECT_THAT([&] { env.set("", ""); },
                ThrowsMessage<std::system_error>(HasSubstr("setenv failed")));
    EXPECT_THAT([&] { env.unset(""); },
                ThrowsMessage<std::system_error>(HasSubstr("unsetenv failed")));
  }

  ASSERT_EQ(nullptr, std::getenv("_DWARFS_TEST_SCOPED_ENV_"));

  {
    dwarfs::detail::scoped_env env("_DWARFS_TEST_SCOPED_ENV_", "something");
    EXPECT_EQ("something"sv, std::getenv("_DWARFS_TEST_SCOPED_ENV_"));
  }

  EXPECT_EQ(nullptr, std::getenv("_DWARFS_TEST_SCOPED_ENV_"));
}

TEST(utils, fatal_signal_handler) {
  install_signal_handlers();

#ifdef DWARFS_STACKTRACE_ENABLED
  EXPECT_DEATH(raise(SIGFPE), "Caught signal SIGFPE");
#endif

#ifndef _WIN32
  EXPECT_DEATH(raise(SIGBUS),
               "Caught signal SIGBUS.*export DWARFS_IOLAYER_OPTS=");
#endif
}

TEST(utils, exception_string) {
  try {
    throw std::runtime_error("this is a test error");
    FAIL() << "exception was not thrown";
  } catch (std::exception const& ex) {
    EXPECT_THAT(exception_str(ex),
                HasSubstr("runtime_error: this is a test error"));
  }

  try {
    throw std::system_error(std::make_error_code(std::errc::permission_denied),
                            "this is a test system error");
    FAIL() << "exception was not thrown";
  } catch (...) {
    EXPECT_THAT(exception_str(std::current_exception()),
                HasSubstr("system_error: this is a test system error"));
  }

  class non_std_exception {};

  try {
    throw non_std_exception{};
    FAIL() << "exception was not thrown";
  } catch (...) {
    auto str = exception_str(std::current_exception());
#if defined(_WIN32) || defined(__FreeBSD__)
    EXPECT_THAT(str, HasSubstr("unknown non-standard exception"));
#else
    EXPECT_THAT(str, HasSubstr("non_std_exception"));
#endif
  }
}

TEST(utils, hexdump) {
  using namespace std::string_view_literals;

  std::array<uint8_t, 19> data;
  std::iota(data.begin(), data.end(), 28);

  static constexpr auto expected =
      R"(
00000000  1c 1d 1e 1f 20 21 22 23  24 25 26 27 28 29 2a 2b  |.... !"#$%&'()*+|
00000010  2c 2d 2e                                          |,-.             |
)"sv;

  EXPECT_EQ(expected.substr(expected.find_first_not_of("\r\n")), hexdump(data));
}
