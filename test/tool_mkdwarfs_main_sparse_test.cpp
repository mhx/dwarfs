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

TEST(mkdwarfs_test, huge_sparse_file) {
  std::string const image_file = "test.dwarfs";
  std::string image;
  std::mt19937_64 rng{42};
  test_file_data tfd;
  file_size_t total_data_size{0};

  {
    auto t = mkdwarfs_tester::create_empty();
    t.add_root_dir();
    std::uniform_int_distribution<file_size_t> data_size_dist(1, 2_KiB);
    std::exponential_distribution<double> hole_size_dist(1.0 / (16_GiB));
    for (int i = 0; i < 1'000; ++i) {
      auto const hs = 1 + static_cast<file_size_t>(hole_size_dist(rng));
      auto const ds = data_size_dist(rng);
      tfd.add_hole(hs);
      tfd.add_data(ds, &rng);
      total_data_size += ds;
    }
    t.os->add_file("/sparse", tfd);

    ASSERT_EQ(0,
              t.run({"-i", "/", "-o", image_file, "-l3", "-S16", "-C", "null"}))
        << t.err();
    auto img = t.fa->get_file(image_file);
    ASSERT_TRUE(img);
    image = std::move(img.value());
    auto fs = t.fs_from_file(image_file);

    auto dev = fs.find("/sparse");
    ASSERT_TRUE(dev);
    auto iv = dev->inode();
    EXPECT_TRUE(iv.is_regular_file());
    auto stat = fs.getattr(iv);
    EXPECT_EQ(tfd.size(), stat.size());
    EXPECT_EQ(total_data_size, stat.allocated_size());

    auto const info =
        fs.info_as_json({.features = reader::fsinfo_features::all()});
    auto const& features = info["features"];
    EXPECT_TRUE(std::find(features.begin(), features.end(), "sparsefiles") !=
                features.end())
        << info.dump(2);
    auto const& size_cache = info["full_metadata"]["reg_file_size_cache"];
    ASSERT_EQ(1, size_cache["size_lookup"].size()) << info.dump(2);
    ASSERT_EQ(1, size_cache["allocated_size_lookup"].size()) << info.dump(2);
    EXPECT_EQ(tfd.size(), size_cache["size_lookup"]["0"].get<file_size_t>())
        << info.dump(2);
    EXPECT_EQ(total_data_size,
              size_cache["allocated_size_lookup"]["0"].get<file_size_t>())
        << info.dump(2);

    for (auto const& ext : tfd.extents) {
      if (ext.info.kind == extent_kind::data) {
        auto const size = ext.info.range.size();
        auto const offset = ext.info.range.offset();
        std::error_code ec;
        auto const data = fs.read_string(iv.inode_num(), size, offset, ec);
        EXPECT_FALSE(ec) << "error at offset " << offset << ": "
                         << ec.message();
        EXPECT_EQ(size, data.size()) << "size mismatch at offset " << offset;
        EXPECT_EQ(ext.data, data) << "data mismatch at offset " << offset;
      }
    }
  }
}
