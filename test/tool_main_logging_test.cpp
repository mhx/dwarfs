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

#include <fmt/format.h>

#include <dwarfs/reader/fsinfo_options.h>

#include "test_tool_main_tester.h"

using namespace dwarfs::test;
using namespace dwarfs;

class logging_test : public testing::TestWithParam<std::string_view> {};

TEST_P(logging_test, end_to_end) {
  auto level = GetParam();
  std::string const image_file = "test.dwarfs";

  auto t = mkdwarfs_tester::create_empty();

  t.add_root_dir();
  t.os->add_local_files(audio_data_dir);
  t.os->add_local_files(fits_data_dir);
  t.os->add_file("random", 4096, true);

  ASSERT_EQ(0, t.run({"-i", "/", "-o", image_file, "--categorize",
                      fmt::format("--log-level={}", level)}));

  auto fs = t.fs_from_file(image_file);

  auto dev16 = fs.find("/test8.aiff");
  auto dev32 = fs.find("/test8.caf");

  EXPECT_TRUE(dev16);
  EXPECT_TRUE(dev32);

  {
    std::vector<std::string> dumps;

    for (int detail = 0; detail <= 6; ++detail) {
      std::ostringstream os;
      fs.dump(os, {.features = reader::fsinfo_features::for_level(detail)});
      auto d = os.str();
      if (!dumps.empty()) {
        EXPECT_GT(d.size(), dumps.back().size()) << detail;
      }
      dumps.emplace_back(std::move(d));
    }

    EXPECT_GT(dumps.back().size(), 10'000);
  }

  {
    std::vector<std::string> infos;

    for (int detail = 0; detail <= 4; ++detail) {
      auto info = fs.info_as_json(
          {.features = reader::fsinfo_features::for_level(detail)});
      auto i = info.dump();
      if (!infos.empty()) {
        EXPECT_GT(i.size(), infos.back().size()) << detail;
      }
      infos.emplace_back(std::move(i));
    }

    EXPECT_GT(infos.back().size(), 1'000);
  }
}

INSTANTIATE_TEST_SUITE_P(mkdwarfs, logging_test,
                         ::testing::ValuesIn(log_level_strings));

class term_logging_test
    : public testing::TestWithParam<std::tuple<std::string_view, bool>> {};

TEST_P(term_logging_test, end_to_end) {
  auto const [level, fancy] = GetParam();
  std::map<std::string_view, std::pair<std::string_view, char>> const match{
      {"error", {"<bold-red>", 'E'}},
      {"warn", {"<bold-yellow>", 'W'}},
      {"info", {"", 'I'}},
      {"verbose", {"<dim-cyan>", 'V'}},
      {"debug", {"<dim-yellow>", 'D'}},
      {"trace", {"<gray>", 'T'}},
  };
  auto const cutoff =
      std::find(log_level_strings.begin(), log_level_strings.end(), level);
  ASSERT_FALSE(cutoff == log_level_strings.end());

  {
    mkdwarfs_tester t;
    t.iol->set_terminal_is_tty(fancy);
    t.iol->set_terminal_fancy(fancy);
    t.os->set_access_fail("/somedir/ipsum.py"); // trigger an error
    EXPECT_EQ(2, t.run("-l1 -i / -o - --categorize --num-workers=8 -S 22 "
                       "-L 16M --progress=none --log-level=" +
                       std::string(level)))
        << t.err();

    auto err = t.err();
    auto it = log_level_strings.begin();

    auto make_contains_regex = [fancy = fancy](auto m) {
      auto const& [color, prefix] = m->second;
      auto beg = fancy ? color : std::string_view{};
      auto end = fancy && !color.empty() ? "<normal>" : "";
      return fmt::format("{}{}\\s\\d\\d:\\d\\d:\\d\\d.*{}\\r?\\n", beg, prefix,
                         end);
    };

    while (it != cutoff + 1) {
      auto m = match.find(*it);
      EXPECT_FALSE(m == match.end());
      auto re = make_contains_regex(m);
      EXPECT_TRUE(std::regex_search(err, std::regex(re))) << re << ", " << err;
      ++it;
    }

    while (it != log_level_strings.end()) {
      auto m = match.find(*it);
      EXPECT_FALSE(m == match.end());
      auto re = make_contains_regex(m);
      EXPECT_FALSE(std::regex_search(err, std::regex(re))) << re << ", " << err;
      ++it;
    }
  }
}

INSTANTIATE_TEST_SUITE_P(
    mkdwarfs, term_logging_test,
    ::testing::Combine(::testing::ValuesIn(log_level_strings),
                       ::testing::Bool()));

TEST(mkdwarfs_test, no_log_context) {
  mkdwarfs_tester t;
  EXPECT_EQ(0, t.run("-l3 -i / -o -")) << t.err();
  EXPECT_THAT(t.err(), ::testing::Not(::testing::HasSubstr("[scanner.cpp:")));
}

TEST(mkdwarfs_test, default_log_context) {
  mkdwarfs_tester t;
  EXPECT_EQ(0, t.run("-l3 -i / -o - --log-level=verbose")) << t.err();
  EXPECT_THAT(t.err(), ::testing::HasSubstr("[scanner.cpp:"));
}

TEST(mkdwarfs_test, explicit_log_context) {
  mkdwarfs_tester t;
  EXPECT_EQ(0, t.run("-l3 -i / -o - --log-with-context")) << t.err();
  EXPECT_THAT(t.err(), ::testing::HasSubstr("[scanner.cpp:"));
}
