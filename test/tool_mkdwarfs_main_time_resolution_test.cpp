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

#include "test_tool_main_checks.h"
#include "test_tool_main_tester.h"

using namespace dwarfs::test;
using namespace dwarfs;

namespace fs = std::filesystem;

using namespace std::literals::string_view_literals;
using namespace dwarfs::binary_literals;

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
  auto const info = fsinfo_json(fs, 2);
  EXPECT_EQ(1, info["time_resolution"].get<int>());
  EXPECT_EQ(1.0f, info["time_resolution"].get<float>());

  ASSERT_NO_FATAL_FAILURE(
      expect_attrs(fs, {{"/",
                         {.type = posix_file_type::directory,
                          .atimespec = make_ts(1, 0),
                          .mtimespec = make_ts(3, 0),
                          .ctimespec = make_ts(5, 0)}},
                        {"/bar.pl",
                         {.type = posix_file_type::regular,
                          .atimespec = make_ts(1001001, 0),
                          .mtimespec = make_ts(3003003, 0),
                          .ctimespec = make_ts(5005005, 0)}}}));
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
    image = t.get_file(image_file);
    auto fs = t.fs_from_file(image_file);

    auto const info = fsinfo_json(fs, 2);
    EXPECT_FLOAT_EQ(1e-9f, info["time_resolution"].get<float>());

    ASSERT_NO_FATAL_FAILURE(
        expect_attrs(fs, {{"/",
                           {.type = posix_file_type::directory,
                            .atimespec = make_ts(1, 2),
                            .mtimespec = make_ts(3, 4),
                            .ctimespec = make_ts(5, 6)}},
                          {"/dir",
                           {.type = posix_file_type::directory,
                            .atimespec = make_ts(10, 20),
                            .mtimespec = make_ts(30, 40),
                            .ctimespec = make_ts(50, 60)}},
                          {"/bar.pl",
                           {.type = posix_file_type::regular,
                            .atimespec = make_ts(1001001, 2002002),
                            .mtimespec = make_ts(3003003, 4004004),
                            .ctimespec = make_ts(5005005, 6006006)}},
                          {"/dir/foo.pl",
                           {.type = posix_file_type::regular,
                            .atimespec = make_ts(2001, 5002),
                            .mtimespec = make_ts(4003, 7004),
                            .ctimespec = make_ts(6005, 9006)}}}));
  }

  auto rebuild_tester = [&image_file](std::string const& image_data) {
    return mkdwarfs_tester::create_with_image(image_data, image_file);
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

    auto const info = fsinfo_json(fs, 2);
    EXPECT_FLOAT_EQ(25e-9f, info["time_resolution"].get<float>());

    ASSERT_NO_FATAL_FAILURE(
        expect_attrs(fs, {{"/",
                           {.type = posix_file_type::directory,
                            .atimespec = make_ts(1, 0),
                            .mtimespec = make_ts(3, 0),
                            .ctimespec = make_ts(5, 0)}},
                          {"/dir",
                           {.type = posix_file_type::directory,
                            .atimespec = make_ts(10, 0),
                            .mtimespec = make_ts(30, 25),
                            .ctimespec = make_ts(50, 50)}},
                          {"/bar.pl",
                           {.type = posix_file_type::regular,
                            .atimespec = make_ts(1001001, 2002000),
                            .mtimespec = make_ts(3003003, 4004000),
                            .ctimespec = make_ts(5005005, 6006000)}},
                          {"/dir/foo.pl",
                           {.type = posix_file_type::regular,
                            .atimespec = make_ts(2001, 5000),
                            .mtimespec = make_ts(4003, 7000),
                            .ctimespec = make_ts(6005, 9000)}}}));
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

    auto const info = fsinfo_json(fs, 2);
    EXPECT_FLOAT_EQ(25e-9f, info["time_resolution"].get<float>());

    ASSERT_NO_FATAL_FAILURE(
        expect_attrs(fs, {{"/",
                           {.type = posix_file_type::directory,
                            .atimespec = make_ts(1, 0),
                            .mtimespec = make_ts(3, 0),
                            .ctimespec = make_ts(5, 0)}},
                          {"/dir",
                           {.type = posix_file_type::directory,
                            .atimespec = make_ts(10, 0),
                            .mtimespec = make_ts(30, 25),
                            .ctimespec = make_ts(50, 50)}}}));
  }

  {
    auto t = rebuild_tester(image);
    ASSERT_EQ(0, t.run({"-i", image_file, "-o", "-", "--rebuild-metadata",
                        "--keep-all-times", "--time-resolution=1us"}))
        << t.err();
    auto fs = t.fs_from_stdout();
    image = t.out();

    auto const info = fsinfo_json(fs, 2);
    EXPECT_FLOAT_EQ(1e-6f, info["time_resolution"].get<float>());

    ASSERT_NO_FATAL_FAILURE(
        expect_attrs(fs, {{"/",
                           {.type = posix_file_type::directory,
                            .atimespec = make_ts(1, 0),
                            .mtimespec = make_ts(3, 0),
                            .ctimespec = make_ts(5, 0)}},
                          {"/dir",
                           {.type = posix_file_type::directory,
                            .atimespec = make_ts(10, 0),
                            .mtimespec = make_ts(30, 0),
                            .ctimespec = make_ts(50, 0)}},
                          {"/bar.pl",
                           {.type = posix_file_type::regular,
                            .atimespec = make_ts(1001001, 2002000),
                            .mtimespec = make_ts(3003003, 4004000),
                            .ctimespec = make_ts(5005005, 6006000)}},
                          {"/dir/foo.pl",
                           {.type = posix_file_type::regular,
                            .atimespec = make_ts(2001, 5000),
                            .mtimespec = make_ts(4003, 7000),
                            .ctimespec = make_ts(6005, 9000)}}}));
  }

  {
    auto t = rebuild_tester(image);
    ASSERT_EQ(0, t.run({"-i", image_file, "-o", "-", "--rebuild-metadata",
                        "--keep-all-times", "--time-resolution=2s"}))
        << t.err();
    auto fs = t.fs_from_stdout();
    image = t.out();

    auto const info = fsinfo_json(fs, 2);
    EXPECT_FLOAT_EQ(2.0f, info["time_resolution"].get<float>());

    ASSERT_NO_FATAL_FAILURE(
        expect_attrs(fs, {{"/",
                           {.type = posix_file_type::directory,
                            .atimespec = make_ts(0, 0),
                            .mtimespec = make_ts(2, 0),
                            .ctimespec = make_ts(4, 0)}},
                          {"/dir",
                           {.type = posix_file_type::directory,
                            .atimespec = make_ts(10, 0),
                            .mtimespec = make_ts(30, 0),
                            .ctimespec = make_ts(50, 0)}},
                          {"/bar.pl",
                           {.type = posix_file_type::regular,
                            .atimespec = make_ts(1001000, 0),
                            .mtimespec = make_ts(3003002, 0),
                            .ctimespec = make_ts(5005004, 0)}},
                          {"/dir/foo.pl",
                           {.type = posix_file_type::regular,
                            .atimespec = make_ts(2000, 0),
                            .mtimespec = make_ts(4002, 0),
                            .ctimespec = make_ts(6004, 0)}}}));
  }
}
