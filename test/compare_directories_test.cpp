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

#include <dwarfs/internal/io_ops.h>
#include <dwarfs/internal/mappable_file.h>

#include "compare_directories.h"
#include "sparse_file_builder.h"
#include "test_helpers.h"

namespace fs = std::filesystem;
using namespace dwarfs;
using namespace dwarfs::test;

using ::testing::AnyOf;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::Not;

class compare_directories_test : public ::testing::Test {
 protected:
  void SetUp() override {
    tempdir.emplace("dwarfs");
    td = tempdir->path();
    dir1 = td / "dir1";
    dir2 = td / "dir2";

    ASSERT_TRUE(fs::create_directory(dir1));
    ASSERT_TRUE(fs::create_directory(dir2));
  }

  void TearDown() override {
    tempdir.reset();
    td.clear();
    dir1.clear();
    dir2.clear();
  }

  std::optional<temporary_directory> tempdir;
  fs::path td;
  fs::path dir1;
  fs::path dir2;
};

TEST_F(compare_directories_test, sanity) {
  write_file(dir1 / "file1.txt", "hello");
  write_file(dir1 / "file2.txt", "world");

  write_file(dir2 / "file1.txt", "hello");
  write_file(dir2 / "file2.txt", "world");

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
    EXPECT_THAT(oss_str, HasSubstr("Matching regular files (2):"));
    EXPECT_THAT(oss_str, Not(HasSubstr("Matching directories")));
    EXPECT_THAT(oss_str, Not(HasSubstr("Matching symlinks")));
    EXPECT_THAT(oss_str, Not(HasSubstr("Differences")));
  }

  write_file(dir2 / "file2.txt", "WORLD");

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
    EXPECT_THAT(oss_str, HasSubstr("Matching regular files (1):"));
    EXPECT_THAT(oss_str, Not(HasSubstr("Matching directories")));
    EXPECT_THAT(oss_str, Not(HasSubstr("Matching symlinks")));
    EXPECT_THAT(oss_str, HasSubstr("Differences (1):"));
  }

  write_file(dir2 / "file3.txt", "new file");

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
    EXPECT_THAT(oss_str, HasSubstr("Matching regular files (1):"));
    EXPECT_THAT(oss_str, Not(HasSubstr("Matching directories")));
    EXPECT_THAT(oss_str, Not(HasSubstr("Matching symlinks")));
    EXPECT_THAT(oss_str, HasSubstr("Differences (2):"));
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
    EXPECT_THAT(oss_str, HasSubstr("Matching regular files (1):"));
    EXPECT_THAT(oss_str, HasSubstr("Matching directories (1):"));
    EXPECT_THAT(oss_str, HasSubstr("Matching symlinks (1):"));
    EXPECT_THAT(oss_str, HasSubstr("Differences (2):"));
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
    EXPECT_THAT(oss_str, HasSubstr("Matching regular files (1):"));
    EXPECT_THAT(oss_str, HasSubstr("Matching directories (1):"));
    EXPECT_THAT(oss_str, HasSubstr("Matching symlinks (1):"));
    EXPECT_THAT(oss_str, HasSubstr("Differences (4):"));
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
    EXPECT_THAT(oss_str, HasSubstr("Matching regular files (1):"));
    EXPECT_THAT(oss_str, HasSubstr("Matching directories (1):"));
    EXPECT_THAT(oss_str, Not(HasSubstr("Matching symlinks")));
    EXPECT_THAT(oss_str, HasSubstr("Differences (5):"));
  }
}

TEST_F(compare_directories_test, sparse_files_sanity) {
  auto const granularity = sparse_file_builder::hole_granularity(td);

  if (!granularity) {
    GTEST_SKIP() << "filesystem does not support sparse files";
  }

  auto const data = create_random_string(granularity.value());
  auto const& ops = internal::get_native_memory_mapping_ops();

  {
    auto const path = dir1 / "sparse.bin";
    auto sfb = sparse_file_builder::create(path);
    sfb.truncate(3 * granularity.value());
    sfb.write_data(0, data);
    sfb.write_data(2 * granularity.value(), data);
    // Holes *must* be punched after all data is written, at least on macOS.
    sfb.punch_hole(granularity.value(), granularity.value());
    sfb.commit();

    auto mf = internal::mappable_file::create(ops, path);
    EXPECT_EQ(3 * granularity.value(), mf.size());
    auto const extents = mf.get_extents();
    EXPECT_EQ(3, extents.size());
  }

  {
    auto const path = dir2 / "sparse.bin";
    write_file(path, data + std::string(granularity.value(), '\0') + data);

    auto mf = internal::mappable_file::create(ops, path);
    EXPECT_EQ(3 * granularity.value(), mf.size());
    auto const extents = mf.get_extents();
    // check that we have either 1 or 3 extents; some filesystems (like
    // ZFS) can be pretty fast about detecting holes
    EXPECT_THAT(extents.size(), AnyOf(Eq(1), Eq(3)));
  }

  {
    auto const cdr = compare_directories(dir1, dir2);
    EXPECT_TRUE(cdr.identical()) << cdr;
  }

  {
    entry_diff ed;
    test::detail::compare_files(dir1 / "sparse.bin", dir2 / "sparse.bin", ed);
    EXPECT_EQ(ed.ranges.size(), 0);
  }

  {
    entry_diff ed;
    test::detail::compare_files(dir1 / "sparse.bin", dir2 / "sparse.bin", ed,
                                true);
    if (ed.ranges.size() > 0) {
      EXPECT_EQ(ed.ranges.size(), 1);
      EXPECT_EQ(ed.ranges[0].range, file_range(0, 3 * granularity.value()));
    }
  }
}

TEST_F(compare_directories_test, size_mismatch) {
  write_file(dir1 / "file1.txt", "hello");
  write_file(dir2 / "file1.txt", "hello world");

  auto const cdr = compare_directories(dir1, dir2);
  ASSERT_FALSE(cdr.identical()) << cdr;

  std::ostringstream oss;
  oss << cdr;
  auto const oss_str = oss.str();
  EXPECT_THAT(oss_str, HasSubstr("Differences (1):")) << cdr;
  EXPECT_THAT(oss_str,
              HasSubstr("File size differs: file1.txt (left=5, right=11)"))
      << cdr;
}

TEST_F(compare_directories_test, type_mismatch) {
  write_file(dir1 / "file1.txt", "hello");
  ASSERT_TRUE(fs::create_directory(dir2 / "file1.txt"));

  auto const cdr = compare_directories(dir1, dir2);
  ASSERT_FALSE(cdr.identical()) << cdr;

  std::ostringstream oss;
  oss << cdr;
  auto const oss_str = oss.str();
  EXPECT_THAT(oss_str, HasSubstr("Differences (1):")) << cdr;
  EXPECT_THAT(
      oss_str,
      HasSubstr("Type mismatch: file1.txt (left=regular, right=directory)"))
      << cdr;
}

TEST(compare_directories, error_not_accessible) {
  temporary_directory tempdir("dwarfs");
  auto const td = tempdir.path();

  auto const dir1 = td / "dir1";
  auto const dir2 = td / "dir2";

  ASSERT_TRUE(fs::create_directory(dir1));

  auto const cdr = compare_directories(dir1, dir2);
  ASSERT_FALSE(cdr.identical()) << cdr;

  std::ostringstream oss;
  oss << cdr;
  auto const oss_str = oss.str();
  EXPECT_THAT(oss_str, HasSubstr("Differences (1):")) << cdr;
  EXPECT_THAT(oss_str, HasSubstr("right_root is not accessible")) << cdr;
}

TEST(compare_directories, error_not_directory) {
  temporary_directory tempdir("dwarfs");
  auto const td = tempdir.path();

  auto const dir1 = td / "dir1";
  auto const dir2 = td / "dir2";

  ASSERT_TRUE(fs::create_directory(dir1));
  write_file(dir2, "I am not a directory");

  auto const cdr = compare_directories(dir1, dir2);
  ASSERT_FALSE(cdr.identical()) << cdr;

  std::ostringstream oss;
  oss << cdr;
  auto const oss_str = oss.str();
  EXPECT_THAT(oss_str, HasSubstr("Differences (1):")) << cdr;
  EXPECT_THAT(oss_str, HasSubstr("right_root is not a directory")) << cdr;
}
