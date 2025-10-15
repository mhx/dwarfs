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

#include <dwarfs/reader/fsinfo_options.h>

#include "test_tool_main_tester.h"

using namespace dwarfs::test;
using namespace dwarfs;

namespace fs = std::filesystem;

using namespace std::literals::string_view_literals;
using namespace dwarfs::binary_literals;
using ts = file_stat::timespec_type;

namespace {

constexpr auto make_ts(file_stat::time_type sec, uint32_t nsec) {
  return file_stat::timespec_type{.sec = sec, .nsec = nsec};
}

} // namespace

using testing::HasSubstr;

TEST(mkdwarfs_test, time_resolution_default) {
  auto t = mkdwarfs_tester::create_empty();

  t.os->add_dir("/", {.atim = {{1, 2}}, .mtim = {{3, 4}}, .ctim = {{5, 6}}});
  t.os->add_file("/bar.pl", 10, true,
                 {
                     .atim = {{1001001, 2002002}},
                     .mtim = {{3003003, 4004004}},
                     .ctim = {{5005005, 6006006}},
                 });

  ASSERT_EQ(0, t.run({"-i", "/", "-o", "-", "--keep-all-times"})) << t.err();
  auto fs = t.fs_from_stdout();

  // ensure that by default, times are stored with second resolution
  auto const info =
      fs.info_as_json({.features = reader::fsinfo_features::for_level(2)});
  EXPECT_EQ(1, info["time_resolution"].get<int>());
  EXPECT_EQ(1.0f, info["time_resolution"].get<float>());

  {
    auto dev = fs.find("/");
    ASSERT_TRUE(dev);
    auto iv = dev->inode();
    EXPECT_TRUE(iv.is_directory());
    auto stat = fs.getattr(iv);
    EXPECT_EQ(make_ts(1, 0), stat.atimespec());
    EXPECT_EQ(make_ts(3, 0), stat.mtimespec());
    EXPECT_EQ(make_ts(5, 0), stat.ctimespec());
  }

  {
    auto dev = fs.find("/bar.pl");
    ASSERT_TRUE(dev);
    auto iv = dev->inode();
    EXPECT_TRUE(iv.is_regular_file());
    auto stat = fs.getattr(iv);
    EXPECT_EQ(make_ts(1001001, 0), stat.atimespec());
    EXPECT_EQ(make_ts(3003003, 0), stat.mtimespec());
    EXPECT_EQ(make_ts(5005005, 0), stat.ctimespec());
  }
}

TEST(mkdwarfs_test, time_resolution_finer_than_native) {
  mkdwarfs_tester t;

  t.os->set_native_file_time_resolution(std::chrono::microseconds(10));

  EXPECT_EQ(0, t.run({"-i", "/", "-o", "-", "--keep-all-times",
                      "--time-resolution=ns"}))
      << t.err();

  EXPECT_THAT(t.err(),
              HasSubstr("requested time resolution of 1ns is finer than the "
                        "native file timestamp resolution of 10us"));
}

TEST(mkdwarfs_test, subsecond_time_resolution) {
  std::string const image_file = "test.dwarfs";
  std::string image;

  {
    auto t = mkdwarfs_tester::create_empty();

    t.os->add_dir("/", {.atim = {{1, 2}}, .mtim = {{3, 4}}, .ctim = {{5, 6}}});
    t.os->add_dir("/dir",
                  {.atim = {{10, 20}}, .mtim = {{30, 40}}, .ctim = {{50, 60}}});
    t.os->add_file("/bar.pl", 10, true,
                   {
                       .atim = {{1001001, 2002002}},
                       .mtim = {{3003003, 4004004}},
                       .ctim = {{5005005, 6006006}},
                   });
    t.os->add_file("/dir/foo.pl", 10, true,
                   {
                       .atim = {{2001, 5002}},
                       .mtim = {{4003, 7004}},
                       .ctim = {{6005, 9006}},
                   });

    ASSERT_EQ(0, t.run({"-i", "/", "-o", image_file, "--keep-all-times",
                        "--time-resolution=ns"}))
        << t.err();
    auto img = t.fa->get_file(image_file);
    ASSERT_TRUE(img);
    image = std::move(img.value());
    auto fs = t.fs_from_file(image_file);

    auto const info =
        fs.info_as_json({.features = reader::fsinfo_features::for_level(2)});
    EXPECT_FLOAT_EQ(1e-9f, info["time_resolution"].get<float>());

    {
      auto dev = fs.find("/");
      ASSERT_TRUE(dev);
      auto iv = dev->inode();
      EXPECT_TRUE(iv.is_directory());
      auto stat = fs.getattr(iv);
      EXPECT_EQ(make_ts(1, 2), stat.atimespec());
      EXPECT_EQ(make_ts(3, 4), stat.mtimespec());
      EXPECT_EQ(make_ts(5, 6), stat.ctimespec());
    }

    {
      auto dev = fs.find("/dir");
      ASSERT_TRUE(dev);
      auto iv = dev->inode();
      EXPECT_TRUE(iv.is_directory());
      auto stat = fs.getattr(iv);
      EXPECT_EQ(make_ts(10, 20), stat.atimespec());
      EXPECT_EQ(make_ts(30, 40), stat.mtimespec());
      EXPECT_EQ(make_ts(50, 60), stat.ctimespec());
    }

    {
      auto dev = fs.find("/bar.pl");
      ASSERT_TRUE(dev);
      auto iv = dev->inode();
      EXPECT_TRUE(iv.is_regular_file());
      auto stat = fs.getattr(iv);
      EXPECT_EQ(make_ts(1001001, 2002002), stat.atimespec());
      EXPECT_EQ(make_ts(3003003, 4004004), stat.mtimespec());
      EXPECT_EQ(make_ts(5005005, 6006006), stat.ctimespec());
    }

    {
      auto dev = fs.find("/dir/foo.pl");
      ASSERT_TRUE(dev);
      auto iv = dev->inode();
      EXPECT_TRUE(iv.is_regular_file());
      auto stat = fs.getattr(iv);
      EXPECT_EQ(make_ts(2001, 5002), stat.atimespec());
      EXPECT_EQ(make_ts(4003, 7004), stat.mtimespec());
      EXPECT_EQ(make_ts(6005, 9006), stat.ctimespec());
    }
  }

  auto rebuild_tester = [&image_file](std::string const& image_data) {
    auto t = mkdwarfs_tester::create_empty();
    t.add_root_dir();
    t.os->add_file(image_file, image_data);
    return t;
  };

  {
    auto t = rebuild_tester(image);
    ASSERT_EQ(1, t.run({"-i", image_file, "-o", "-", "--rebuild-metadata",
                        "--keep-all-times", "--time-resolution=30ns"}))
        << t.err();

    EXPECT_THAT(t.err(),
                HasSubstr("cannot handle subsecond resolution (30ns) that is "
                          "not a whole divisor of one second"));
  }

  {
    auto t = rebuild_tester(image);
    ASSERT_EQ(0, t.run({"-i", image_file, "-o", "-", "--rebuild-metadata",
                        "--keep-all-times", "--time-resolution=25ns"}))
        << t.err();
    auto fs = t.fs_from_stdout();
    image = t.out();

    auto const info =
        fs.info_as_json({.features = reader::fsinfo_features::for_level(2)});
    EXPECT_FLOAT_EQ(25e-9f, info["time_resolution"].get<float>());

    {
      auto dev = fs.find("/");
      ASSERT_TRUE(dev);
      auto iv = dev->inode();
      EXPECT_TRUE(iv.is_directory());
      auto stat = fs.getattr(iv);
      EXPECT_EQ(make_ts(1, 0), stat.atimespec());
      EXPECT_EQ(make_ts(3, 0), stat.mtimespec());
      EXPECT_EQ(make_ts(5, 0), stat.ctimespec());
    }

    {
      auto dev = fs.find("/dir");
      ASSERT_TRUE(dev);
      auto iv = dev->inode();
      EXPECT_TRUE(iv.is_directory());
      auto stat = fs.getattr(iv);
      EXPECT_EQ(make_ts(10, 0), stat.atimespec());
      EXPECT_EQ(make_ts(30, 25), stat.mtimespec());
      EXPECT_EQ(make_ts(50, 50), stat.ctimespec());
    }

    {
      auto dev = fs.find("/bar.pl");
      ASSERT_TRUE(dev);
      auto iv = dev->inode();
      EXPECT_TRUE(iv.is_regular_file());
      auto stat = fs.getattr(iv);
      EXPECT_EQ(make_ts(1001001, 2002000), stat.atimespec());
      EXPECT_EQ(make_ts(3003003, 4004000), stat.mtimespec());
      EXPECT_EQ(make_ts(5005005, 6006000), stat.ctimespec());
    }

    {
      auto dev = fs.find("/dir/foo.pl");
      ASSERT_TRUE(dev);
      auto iv = dev->inode();
      EXPECT_TRUE(iv.is_regular_file());
      auto stat = fs.getattr(iv);
      EXPECT_EQ(make_ts(2001, 5000), stat.atimespec());
      EXPECT_EQ(make_ts(4003, 7000), stat.mtimespec());
      EXPECT_EQ(make_ts(6005, 9000), stat.ctimespec());
    }
  }

  {
    auto t = rebuild_tester(image);
    ASSERT_EQ(1, t.run({"-i", image_file, "-o", "-", "--rebuild-metadata",
                        "--keep-all-times", "--time-resolution=40ns"}))
        << t.err();

    EXPECT_THAT(
        t.err(),
        HasSubstr("cannot convert time to a coarser resolution (40ns) that is "
                  "not a whole multiple of the old resolution (25ns)"));
  }

  {
    auto t = rebuild_tester(image);
    ASSERT_EQ(1, t.run({"-i", image_file, "-o", "-", "--rebuild-metadata",
                        "--keep-all-times", "--time-resolution=5ns"}))
        << t.err();

    EXPECT_THAT(t.err(), HasSubstr("cannot convert time to a finer resolution "
                                   "(5ns) than the old resolution (25ns)"));
  }

  // not explicitly specifying a time resolution should keep the existing one
  {
    auto t = rebuild_tester(image);
    ASSERT_EQ(0, t.run({"-i", image_file, "-o", "-", "--rebuild-metadata",
                        "--keep-all-times"}))
        << t.err();
    auto fs = t.fs_from_stdout();
    image = t.out();

    auto const info =
        fs.info_as_json({.features = reader::fsinfo_features::for_level(2)});
    EXPECT_FLOAT_EQ(25e-9f, info["time_resolution"].get<float>());

    {
      auto dev = fs.find("/");
      ASSERT_TRUE(dev);
      auto iv = dev->inode();
      EXPECT_TRUE(iv.is_directory());
      auto stat = fs.getattr(iv);
      EXPECT_EQ(make_ts(1, 0), stat.atimespec());
      EXPECT_EQ(make_ts(3, 0), stat.mtimespec());
      EXPECT_EQ(make_ts(5, 0), stat.ctimespec());
    }

    {
      auto dev = fs.find("/dir");
      ASSERT_TRUE(dev);
      auto iv = dev->inode();
      EXPECT_TRUE(iv.is_directory());
      auto stat = fs.getattr(iv);
      EXPECT_EQ(make_ts(10, 0), stat.atimespec());
      EXPECT_EQ(make_ts(30, 25), stat.mtimespec());
      EXPECT_EQ(make_ts(50, 50), stat.ctimespec());
    }
  }
}
