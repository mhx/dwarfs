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
#include <gtest/gtest.h>

#include <dwarfs/file_util.h>

#include "compare_directories.h"

TEST(compare_directories, sanity) {
  namespace fs = std::filesystem;
  using dwarfs::test::compare_directories;

  dwarfs::temporary_directory tempdir("dwarfs");
  auto td = tempdir.path();
  auto dir1 = td / "dir1";
  auto dir2 = td / "dir2";

  ASSERT_TRUE(fs::create_directory(dir1));
  ASSERT_TRUE(fs::create_directory(dir2));

  dwarfs::write_file(dir1 / "file1.txt", "hello");
  dwarfs::write_file(dir1 / "file2.txt", "world");

  dwarfs::write_file(dir2 / "file1.txt", "hello");
  dwarfs::write_file(dir2 / "file2.txt", "world");

  {
    auto const cdr = compare_directories(dir1, dir2);
    ASSERT_TRUE(cdr.identical()) << cdr;
    EXPECT_EQ(cdr.matching_regular_files.size(), 2) << cdr;
    EXPECT_EQ(cdr.matching_directories.size(), 0) << cdr;
    EXPECT_EQ(cdr.matching_symlinks.size(), 0) << cdr;
    EXPECT_EQ(cdr.differences.size(), 0) << cdr;
    EXPECT_EQ(cdr.total_matching_regular_file_size, 10) << cdr;

    std::ostringstream oss;
    oss << cdr;
    auto const oss_str = oss.str();
    EXPECT_THAT(oss_str, ::testing::HasSubstr("Matching regular files (2):"));
    EXPECT_THAT(oss_str,
                ::testing::Not(::testing::HasSubstr("Matching directories")));
    EXPECT_THAT(oss_str,
                ::testing::Not(::testing::HasSubstr("Matching symlinks")));
    EXPECT_THAT(oss_str, ::testing::Not(::testing::HasSubstr("Differences")));
  }

  dwarfs::write_file(dir2 / "file2.txt", "WORLD");

  {
    auto const cdr = compare_directories(dir1, dir2);
    ASSERT_FALSE(cdr.identical()) << cdr;
    EXPECT_EQ(cdr.matching_regular_files.size(), 1) << cdr;
    EXPECT_EQ(cdr.matching_directories.size(), 0) << cdr;
    EXPECT_EQ(cdr.matching_symlinks.size(), 0) << cdr;
    EXPECT_EQ(cdr.differences.size(), 1) << cdr;
    EXPECT_EQ(cdr.total_matching_regular_file_size, 5) << cdr;

    std::ostringstream oss;
    oss << cdr;
    auto const oss_str = oss.str();
    EXPECT_THAT(oss_str, ::testing::HasSubstr("Matching regular files (1):"));
    EXPECT_THAT(oss_str,
                ::testing::Not(::testing::HasSubstr("Matching directories")));
    EXPECT_THAT(oss_str,
                ::testing::Not(::testing::HasSubstr("Matching symlinks")));
    EXPECT_THAT(oss_str, ::testing::HasSubstr("Differences (1):"));
  }

  dwarfs::write_file(dir2 / "file3.txt", "new file");

  {
    auto const cdr = compare_directories(dir1, dir2);
    ASSERT_FALSE(cdr.identical()) << cdr;
    EXPECT_EQ(cdr.matching_regular_files.size(), 1) << cdr;
    EXPECT_EQ(cdr.matching_directories.size(), 0) << cdr;
    EXPECT_EQ(cdr.matching_symlinks.size(), 0) << cdr;
    EXPECT_EQ(cdr.differences.size(), 2) << cdr;
    EXPECT_EQ(cdr.total_matching_regular_file_size, 5) << cdr;

    std::ostringstream oss;
    oss << cdr;
    auto const oss_str = oss.str();
    EXPECT_THAT(oss_str, ::testing::HasSubstr("Matching regular files (1):"));
    EXPECT_THAT(oss_str,
                ::testing::Not(::testing::HasSubstr("Matching directories")));
    EXPECT_THAT(oss_str,
                ::testing::Not(::testing::HasSubstr("Matching symlinks")));
    EXPECT_THAT(oss_str, ::testing::HasSubstr("Differences (2):"));
  }

  fs::create_symlink("file1.txt", dir1 / "link1");
  fs::create_symlink("file1.txt", dir2 / "link1");

  fs::create_directory(dir1 / "subdir");
  fs::create_directory(dir2 / "subdir");

  {
    auto const cdr = compare_directories(dir1, dir2);
    ASSERT_FALSE(cdr.identical()) << cdr;
    EXPECT_EQ(cdr.matching_regular_files.size(), 1) << cdr;
    EXPECT_EQ(cdr.matching_directories.size(), 1) << cdr;
    EXPECT_EQ(cdr.matching_symlinks.size(), 1) << cdr;
    EXPECT_EQ(cdr.differences.size(), 2) << cdr;
    EXPECT_EQ(cdr.total_matching_regular_file_size, 5) << cdr;

    std::ostringstream oss;
    oss << cdr;
    auto const oss_str = oss.str();
    EXPECT_THAT(oss_str, ::testing::HasSubstr("Matching regular files (1):"));
    EXPECT_THAT(oss_str, ::testing::HasSubstr("Matching directories (1):"));
    EXPECT_THAT(oss_str, ::testing::HasSubstr("Matching symlinks (1):"));
    EXPECT_THAT(oss_str, ::testing::HasSubstr("Differences (2):"));
  }

  fs::create_directory(dir1 / "subdir2");
  fs::create_symlink("file2.txt", dir2 / "link2");

  {
    auto const cdr = compare_directories(dir1, dir2);
    ASSERT_FALSE(cdr.identical()) << cdr;
    EXPECT_EQ(cdr.matching_regular_files.size(), 1) << cdr;
    EXPECT_EQ(cdr.matching_directories.size(), 1) << cdr;
    EXPECT_EQ(cdr.matching_symlinks.size(), 1) << cdr;
    EXPECT_EQ(cdr.differences.size(), 4) << cdr;
    EXPECT_EQ(cdr.total_matching_regular_file_size, 5) << cdr;

    std::ostringstream oss;
    oss << cdr;
    auto const oss_str = oss.str();
    EXPECT_THAT(oss_str, ::testing::HasSubstr("Matching regular files (1):"));
    EXPECT_THAT(oss_str, ::testing::HasSubstr("Matching directories (1):"));
    EXPECT_THAT(oss_str, ::testing::HasSubstr("Matching symlinks (1):"));
    EXPECT_THAT(oss_str, ::testing::HasSubstr("Differences (4):"));
  }

  fs::remove(dir2 / "link1");
  fs::create_symlink("file3.txt", dir2 / "link1");

  {
    auto const cdr = compare_directories(dir1, dir2);
    ASSERT_FALSE(cdr.identical()) << cdr;
    EXPECT_EQ(cdr.matching_regular_files.size(), 1) << cdr;
    EXPECT_EQ(cdr.matching_directories.size(), 1) << cdr;
    EXPECT_EQ(cdr.matching_symlinks.size(), 0) << cdr;
    EXPECT_EQ(cdr.differences.size(), 5) << cdr;
    EXPECT_EQ(cdr.total_matching_regular_file_size, 5) << cdr;

    std::ostringstream oss;
    oss << cdr;
    auto const oss_str = oss.str();
    EXPECT_THAT(oss_str, ::testing::HasSubstr("Matching regular files (1):"));
    EXPECT_THAT(oss_str, ::testing::HasSubstr("Matching directories (1):"));
    EXPECT_THAT(oss_str,
                ::testing::Not(::testing::HasSubstr("Matching symlinks")));
    EXPECT_THAT(oss_str, ::testing::HasSubstr("Differences (5):"));
  }
}
