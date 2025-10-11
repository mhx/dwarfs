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

#include <gmock/gmock.h>

#include <dwarfs/binary_literals.h>
#include <dwarfs/reader/fsinfo_options.h>

#include "test_tool_main_tester.h"

using namespace dwarfs::test;
using namespace dwarfs;

namespace fs = std::filesystem;

using namespace std::literals::string_view_literals;
using namespace dwarfs::binary_literals;

TEST(mkdwarfs_test, build_with_sparse_files_no_sparse) {
  std::string const image_file = "test.dwarfs";
  std::mt19937_64 rng{42};

  auto t = mkdwarfs_tester::create_empty();
  t.add_root_dir();
  t.os->add_file("/sparse", {
                                {extent_kind::data, 10'000, &rng},
                                {extent_kind::hole, 20'000},
                                {extent_kind::data, 10'000, &rng},
                            });

  ASSERT_EQ(0, t.run({"-i", "/", "-o", image_file, "-l3", "--no-sparse-files"}))
      << t.err();
  auto fs = t.fs_from_file(image_file);

  auto dev = fs.find("/sparse");
  ASSERT_TRUE(dev);
  auto iv = dev->inode();
  EXPECT_TRUE(iv.is_regular_file());
  auto stat = fs.getattr(iv);
  EXPECT_EQ(40'000, stat.size());
  EXPECT_EQ(40'000, stat.allocated_size());

  auto const info = fs.info_as_json({});
  auto const& features = info["features"];
  EXPECT_TRUE(std::find(features.begin(), features.end(), "sparsefiles") ==
              features.end())
      << info.dump(2);
}

TEST(mkdwarfs_test, build_with_sparse_files) {
  std::string const image_file = "test.dwarfs";
  std::string image;
  std::mt19937_64 rng{42};

  {
    auto t = mkdwarfs_tester::create_empty();
    t.add_root_dir();
    t.os->add_file("/sparse", {
                                  {extent_kind::data, 10'000, &rng},
                                  {extent_kind::hole, 20'000},
                                  {extent_kind::data, 10'000, &rng},
                              });

    ASSERT_EQ(0, t.run({"-i", "/", "-o", image_file, "-l3"})) << t.err();
    auto img = t.fa->get_file(image_file);
    ASSERT_TRUE(img);
    image = std::move(img.value());
    auto fs = t.fs_from_file(image_file);

    auto dev = fs.find("/sparse");
    ASSERT_TRUE(dev);
    auto iv = dev->inode();
    EXPECT_TRUE(iv.is_regular_file());
    auto stat = fs.getattr(iv);
    EXPECT_EQ(40'000, stat.size());
    EXPECT_EQ(20'000, stat.allocated_size());

    auto const info = fs.info_as_json({});
    auto const& features = info["features"];
    EXPECT_TRUE(std::find(features.begin(), features.end(), "sparsefiles") !=
                features.end())
        << info.dump(2);
  }

  auto rebuild_tester = [&image_file](std::string const& image_data) {
    auto t = mkdwarfs_tester::create_empty();
    t.add_root_dir();
    t.os->add_file(image_file, image_data);
    return t;
  };

  {
    auto t = rebuild_tester(image);
    ASSERT_EQ(0, t.run({"-i", image_file, "-o", "-", "--rebuild-metadata"}))
        << t.err();
    auto fs = t.fs_from_stdout();

    auto dev = fs.find("/sparse");
    ASSERT_TRUE(dev);
    auto iv = dev->inode();
    EXPECT_TRUE(iv.is_regular_file());
    auto stat = fs.getattr(iv);
    EXPECT_EQ(40'000, stat.size());
    EXPECT_EQ(20'000, stat.allocated_size());

    auto const info = fs.info_as_json({});
    auto const& features = info["features"];
    EXPECT_TRUE(std::find(features.begin(), features.end(), "sparsefiles") !=
                features.end())
        << info.dump(2);
  }

  {
    auto t = rebuild_tester(image);
    EXPECT_EQ(1, t.run({"-i", image_file, "-o", "-", "--rebuild-metadata",
                        "--no-sparse-files"}))
        << t.err();
    EXPECT_THAT(
        t.err(),
        testing::HasSubstr(
            "cannot disable sparse files when the input filesystem uses them"));
  }
}
