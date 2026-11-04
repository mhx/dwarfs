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

TEST(mkdwarfs_test, source_date_epoch_clamps_scanned_timestamps) {
  constexpr auto epoch = 100;

  for (bool const keep_all_times : {false, true}) {
    SCOPED_TRACE(keep_all_times);

    auto t = mkdwarfs_tester::create_empty();

    t.os->setenv("SOURCE_DATE_EPOCH", std::to_string(epoch));

    t.os->add_dir("/", {.atim = make_ts(99, 7),
                        .mtim = make_ts(100, 1),
                        .ctim = make_ts(101, 0)});
    t.os->add_dir("/dir", {.atim = make_ts(101, 17),
                           .mtim = make_ts(102, 0),
                           .ctim = make_ts(99, 42)});
    t.os->add_file("/old", "old",
                   {.atim = make_ts(90, 7),
                    .mtim = make_ts(99, 99),
                    .ctim = make_ts(80, 23)});
    t.os->add_file("/boundary", "x",
                   {.atim = make_ts(100, 0),
                    .mtim = make_ts(100, 1),
                    .ctim = make_ts(100, 2)});
    t.os->add_file("/future", "data",
                   {.atim = make_ts(105, 7),
                    .mtim = make_ts(110, 8),
                    .ctim = make_ts(120, 9)});
    t.os->add("/link",
              {.ino = 1000,
               .mode = posix_file_type::symlink | 0777,
               .size = 7,
               .atim = {90, 20},
               .mtim = {102, 5},
               .ctim = {101, 0}},
              "/future");

    std::vector<std::string> args{"-i", "/",   "-o",
                                  "-",  "-l1", "--time-resolution=ns"};
    if (keep_all_times) {
      args.emplace_back("--keep-all-times");
    }

    ASSERT_EQ(0, t.run(args)) << t.err();
    auto fs = t.fs_from_stdout();

    ASSERT_NO_FATAL_FAILURE(
        expect_attrs(fs, {{"/", {.mtimespec = make_ts(100, 0)}},
                          {"/dir", {.mtimespec = make_ts(100, 0)}},
                          {"/old", {.mtimespec = make_ts(99, 99)}},
                          {"/boundary", {.mtimespec = make_ts(100, 0)}},
                          {"/future", {.mtimespec = make_ts(100, 0)}},
                          {"/link", {.mtimespec = make_ts(100, 0)}}}));

    if (keep_all_times) {
      ASSERT_NO_FATAL_FAILURE(expect_attrs(
          fs,
          {{"/", {.atimespec = make_ts(99, 7), .ctimespec = make_ts(100, 0)}},
           {"/dir",
            {.atimespec = make_ts(100, 0), .ctimespec = make_ts(99, 42)}},
           {"/old",
            {.atimespec = make_ts(90, 7), .ctimespec = make_ts(80, 23)}},
           {"/boundary",
            {.atimespec = make_ts(100, 0), .ctimespec = make_ts(100, 0)}},
           {"/future",
            {.atimespec = make_ts(100, 0), .ctimespec = make_ts(100, 0)}},
           {"/link",
            {.atimespec = make_ts(90, 20), .ctimespec = make_ts(100, 0)}}}));
    }
  }
}

TEST(mkdwarfs_test, source_date_epoch_clamping_respects_set_time) {
  auto t = mkdwarfs_tester::create_empty();

  t.os->setenv("SOURCE_DATE_EPOCH", "100");

  t.os->add_dir("/", {.atim = make_ts(200, 9),
                      .mtim = make_ts(200, 9),
                      .ctim = make_ts(200, 9)});
  t.os->add_file("/file", "data",
                 {.atim = make_ts(200, 9),
                  .mtim = make_ts(200, 9),
                  .ctim = make_ts(200, 9)});

  ASSERT_EQ(0, t.run({"-i", "/", "-o", "-", "-l1", "--keep-all-times",
                      "--time-resolution=ns", "--set-time=250"}))
      << t.err();

  auto fs = t.fs_from_stdout();

  ASSERT_NO_FATAL_FAILURE(expect_attrs(fs, {{"/",
                                             {.atimespec = make_ts(250, 0),
                                              .mtimespec = make_ts(250, 0),
                                              .ctimespec = make_ts(250, 0)}},
                                            {"/file",
                                             {.atimespec = make_ts(250, 0),
                                              .mtimespec = make_ts(250, 0),
                                              .ctimespec = make_ts(250, 0)}}}));
}

TEST(mkdwarfs_test, source_date_epoch_clamping_with_reproducible) {
  auto t = mkdwarfs_tester::create_empty();

  t.os->setenv("SOURCE_DATE_EPOCH", "100");
  t.os->add_dir("/", {.mtim = make_ts(110, 0)});
  t.os->add_file("/file", "data", {.mtim = make_ts(200, 0)});

  ASSERT_EQ(0, t.run({"-i", "/", "-o", "-", "-l1", "--reproducible"}))
      << t.err();

  auto fs = t.fs_from_stdout();

  ASSERT_NO_FATAL_FAILURE(
      expect_attrs(fs, {{"/", {.mtime = 100}}, {"/file", {.mtime = 100}}}));

  auto const info = fsinfo_json(fs, 2);

  EXPECT_FALSE(info.contains("created_on"));
  ASSERT_EQ(info.at("history").size(), 1);
  EXPECT_FALSE(info["history"][0].contains("timestamp"));
}
