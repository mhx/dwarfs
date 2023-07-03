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

#include <vector>

#include "dwarfs/util.h"

using namespace dwarfs;

namespace {} // namespace

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
