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

#include <gtest/gtest.h>

#include <array>
#include <numeric>
#include <tuple>
#include <vector>

#include "dwarfs/offset_cache.h"
#include "dwarfs/util.h"

using namespace dwarfs;

TEST(utf8_display_width, basic) {
  EXPECT_EQ(0, utf8_display_width(""));
  EXPECT_EQ(1, utf8_display_width(u8string_to_string(u8"a")));
  EXPECT_EQ(5, utf8_display_width(u8string_to_string(u8"abcde")));
  EXPECT_EQ(2, utf8_display_width(u8string_to_string(u8"你")));
  EXPECT_EQ(4, utf8_display_width(u8string_to_string(u8"我你")));
  EXPECT_EQ(6, utf8_display_width(u8string_to_string(u8"我爱你")));
  EXPECT_EQ(5, utf8_display_width(u8string_to_string(u8"☀️ Sun")));
  EXPECT_EQ(2, utf8_display_width(u8string_to_string(u8"⚽️")));
  EXPECT_EQ(5, utf8_display_width(u8string_to_string(u8"مرحبًا")));
  EXPECT_EQ(50, utf8_display_width(u8string_to_string(
                    u8"unicode/我爱你/☀️ Sun/Γειά σας/مرحبًا/⚽️/Карибського")));
}

TEST(shorten_path, string_ascii) {
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
      EXPECT_EQ(expected[max_len], path);
    }
  }
}

TEST(shorten_path, string_utf8) {
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
      EXPECT_EQ(u8string_to_string(expected[max_len]), path);
    }
  }
}

namespace {

using cache_type = basic_offset_cache<uint32_t, uint32_t, uint32_t, 4, 4>;
constexpr std::array<cache_type::file_offset_type, 32> const test_chunks{
    3, 15, 13, 1,  11, 6,  9,  15, 1,  16, 1,  13, 11, 16, 10, 14,
    4, 14, 4,  16, 8,  12, 16, 2,  16, 10, 15, 15, 2,  15, 5,  8,
};
constexpr cache_type::inode_type const test_inode = 42;
size_t const total_size =
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

  if (ent) {
    ent->update(upd, chunk_index, chunk_offset, *it);
    cache->set(inode, ent);
  }

  return {chunk_index, remaining_offset, num_lookups};
}

} // namespace

TEST(offset_cache, basic) {
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

TEST(offset_cache, prefill) {
  cache_type prefilled_cache(4);

  auto [prefill_ix, prefill_off, prefill_lookups] = find_file_position(
      test_inode, test_chunks, total_size - 1, &prefilled_cache);

  EXPECT_EQ(test_chunks.size(), prefill_lookups);
  EXPECT_EQ(test_chunks.size() - 1, prefill_ix);
  EXPECT_EQ(test_chunks.back() - 1, prefill_off);
}
