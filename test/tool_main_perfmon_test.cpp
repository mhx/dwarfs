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

#include <regex>

#include <gmock/gmock.h>

#include "test_tool_main_tester.h"

using namespace dwarfs::test;

#if DWARFS_PERFMON_ENABLED &&                                                  \
    !defined(DWARFS_FILESYSTEM_EXTRACTOR_NO_OPEN_FORMAT)
TEST(dwarfsextract_test, perfmon) {
  auto t = dwarfsextract_tester::create_with_image();
  ASSERT_EQ(0, t.run({"-i", "image.dwarfs", "-f", "mtree", "--perfmon",
                      "filesystem_v2,inode_reader_v2"}))
      << t.err();
  auto outs = t.out();
  auto errs = t.err();
  EXPECT_GT(outs.size(), 100);
  EXPECT_FALSE(errs.empty());
  EXPECT_THAT(errs, ::testing::HasSubstr("[filesystem_v2.readv_future]"));
  EXPECT_THAT(errs, ::testing::HasSubstr("[filesystem_v2.getattr]"));
  EXPECT_THAT(errs, ::testing::HasSubstr("[filesystem_v2.readlink_ec]"));
  EXPECT_THAT(errs, ::testing::HasSubstr("[inode_reader_v2.readv_future]"));
  static std::regex const perfmon_re{R"(\[filesystem_v2\.getattr\])"
                                     R"(\s+samples:\s+\d+)"
                                     R"(\s+overall:\s+\d+(\.\d+)?[num]?s)"
                                     R"(\s+avg latency:\s+\d+(\.\d+)?[num]?s)"
                                     R"(\s+p50 latency:\s+\d+(\.\d+)?[num]?s)"
                                     R"(\s+p90 latency:\s+\d+(\.\d+)?[num]?s)"
                                     R"(\s+p99 latency:\s+\d+(\.\d+)?[num]?s)"};
  EXPECT_TRUE(std::regex_search(errs, perfmon_re)) << errs;
}

TEST(dwarfsextract_test, perfmon_trace) {
  auto t = dwarfsextract_tester::create_with_image();
  ASSERT_EQ(0, t.run({"-i", "image.dwarfs", "-f", "gnutar", "--perfmon",
                      "filesystem_v2,inode_reader_v2,block_cache",
                      "--perfmon-trace", "trace.json"}))
      << t.err();

  EXPECT_GT(t.out().size(), 1'000'000);

  auto trace_file = t.fa->get_file("trace.json");
  ASSERT_TRUE(trace_file);
  EXPECT_GT(trace_file->size(), 10'000);

  auto trace = nlohmann::json::parse(*trace_file);
  EXPECT_TRUE(trace.is_array());

  std::set<std::string> const expected = {"filesystem_v2", "inode_reader_v2",
                                          "block_cache"};
  std::set<std::string> actual;

  for (auto const& obj : trace) {
    EXPECT_TRUE(obj.is_object());
    EXPECT_TRUE(obj["cat"].is_string());
    actual.insert(obj["cat"].get<std::string>());
  }

  EXPECT_EQ(expected, actual);
}
#endif
