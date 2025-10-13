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

#include <fmt/format.h>
#if FMT_VERSION >= 110000
#include <fmt/ranges.h>
#endif

#include <dwarfs/reader/fsinfo_options.h>

#include "test_tool_main_tester.h"

using namespace dwarfs::test;
using namespace dwarfs;

namespace {

constexpr std::array<std::string_view, 9> const pack_mode_names = {
    "chunk_table", "directories",    "shared_files", "names", "names_index",
    "symlinks",    "symlinks_index", "force",        "plain",
};

}

TEST(mkdwarfs_test, pack_modes_random) {
  DWARFS_SLOW_TEST();

  std::mt19937_64 rng{42};
  std::uniform_int_distribution<> dist{1, pack_mode_names.size()};

  for (int i = 0; i < 50; ++i) {
    std::vector<std::string_view> modes(pack_mode_names.begin(),
                                        pack_mode_names.end());
    std::shuffle(modes.begin(), modes.end(), rng);
    modes.resize(dist(rng));
    auto mode_arg = fmt::format("{}", fmt::join(modes, ","));
    auto t = mkdwarfs_tester::create_empty();
    t.add_test_file_tree();
#ifdef DWARFS_TEST_CROSS_COMPILE
    t.add_random_file_tree({.avg_size = 128.0, .dimension = 10});
#else
    t.add_random_file_tree({.avg_size = 128.0, .dimension = 16});
#endif
    ASSERT_EQ(
        0, t.run({"-i", "/", "-o", "-", "-l1", "--pack-metadata=" + mode_arg}))
        << t.err();
    auto fs = t.fs_from_stdout();
    auto info =
        fs.info_as_json({.features = reader::fsinfo_features::for_level(2)});
    std::set<std::string> ms(modes.begin(), modes.end());
    std::set<std::string> fsopt;
    for (auto const& opt : info["options"]) {
      fsopt.insert(opt.get<std::string>());
    }
    auto ctx = mode_arg + "\n" +
               fs.dump({.features = reader::fsinfo_features::for_level(2)});
    EXPECT_EQ(ms.count("chunk_table"), fsopt.count("packed_chunk_table"))
        << ctx;
    EXPECT_EQ(ms.count("directories"), fsopt.count("packed_directories"))
        << ctx;
    EXPECT_EQ(ms.count("shared_files"),
              fsopt.count("packed_shared_files_table"))
        << ctx;
    if (ms.count("plain")) {
      EXPECT_EQ(0, fsopt.count("packed_names")) << ctx;
      EXPECT_EQ(0, fsopt.count("packed_names_index")) << ctx;
      EXPECT_EQ(0, fsopt.count("packed_symlinks")) << ctx;
      EXPECT_EQ(0, fsopt.count("packed_symlinks_index")) << ctx;
    }
  }
}

TEST(mkdwarfs_test, pack_mode_none) {
  auto t = mkdwarfs_tester::create_empty();
  t.add_test_file_tree();
  t.add_random_file_tree({.avg_size = 128.0, .dimension = 16});
  ASSERT_EQ(0, t.run({"-i", "/", "-o", "-", "-l1", "--pack-metadata=none"}))
      << t.err();
  auto fs = t.fs_from_stdout();
  auto info =
      fs.info_as_json({.features = reader::fsinfo_features::for_level(2)});
  std::set<std::string> fsopt;
  for (auto const& opt : info["options"]) {
    fsopt.insert(opt.get<std::string>());
  }
  fsopt.erase("inodes_have_nlink");
  fsopt.erase("mtime_only");
  EXPECT_TRUE(fsopt.empty()) << info["options"].dump();
}

TEST(mkdwarfs_test, pack_mode_all) {
  auto t = mkdwarfs_tester::create_empty();
  t.add_test_file_tree();
  t.add_random_file_tree({.avg_size = 128.0, .dimension = 16});
  ASSERT_EQ(0, t.run({"-i", "/", "-o", "-", "-l1", "--pack-metadata=all"}))
      << t.err();
  auto fs = t.fs_from_stdout();
  auto info =
      fs.info_as_json({.features = reader::fsinfo_features::for_level(2)});
  std::set<std::string> expected = {
      "packed_chunk_table",   "packed_directories",        "packed_names",
      "packed_names_index",   "packed_shared_files_table", "packed_symlinks",
      "packed_symlinks_index"};
  std::set<std::string> fsopt;
  for (auto const& opt : info["options"]) {
    fsopt.insert(opt.get<std::string>());
  }
  fsopt.erase("inodes_have_nlink");
  fsopt.erase("mtime_only");
  EXPECT_EQ(expected, fsopt) << info["options"].dump();
}

TEST(mkdwarfs_test, pack_mode_invalid) {
  mkdwarfs_tester t;
  EXPECT_NE(0, t.run({"-i", "/", "-o", "-", "--pack-metadata=grmpf"}));
  EXPECT_THAT(t.err(), ::testing::HasSubstr("'--pack-metadata' is invalid"));
}
