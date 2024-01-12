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
 */

#include <filesystem>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <fmt/format.h>

#include "dwarfs/filesystem_v2.h"
#include "dwarfs/mmap.h"

#include "test_logger.h"

using namespace dwarfs;

namespace fs = std::filesystem;

namespace {

auto test_dir = fs::path(TEST_DATA_DIR).make_preferred();

} // namespace

TEST(filesystem, metadata_symlink_win) {
  test::test_logger lgr;

  auto mm = std::make_shared<mmap>(test_dir / "winlink.dwarfs");
  filesystem_v2 fs(lgr, mm);

  auto i1 = fs.find("link.txt");
  auto i2 = fs.find("dir/link.txt");
  auto i3 = fs.find("subdir/test.txt");

  ASSERT_TRUE(i1);
  ASSERT_TRUE(i2);
  ASSERT_TRUE(i3);

  EXPECT_TRUE(i1->is_symlink());
  EXPECT_TRUE(i2->is_symlink());
  EXPECT_TRUE(i3->is_regular_file());

  // readlink_mode::preferred (default)
  {
    std::string buf1, buf2;
    EXPECT_EQ(0, fs.readlink(*i1, &buf1));
    EXPECT_EQ(0, fs.readlink(*i2, &buf2));

#if defined(_WIN32)
    EXPECT_EQ("subdir\\test.txt", buf1);
    EXPECT_EQ("..\\subdir\\test.txt", buf2);
#else
    EXPECT_EQ("subdir/test.txt", buf1);
    EXPECT_EQ("../subdir/test.txt", buf2);
#endif
  }

  {
    std::string buffer;
    EXPECT_EQ(0, fs.readlink(*i1, &buffer, readlink_mode::raw));
    EXPECT_EQ("subdir\\test.txt", buffer);
    EXPECT_EQ(0, fs.readlink(*i2, &buffer, readlink_mode::raw));
    EXPECT_EQ("..\\subdir\\test.txt", buffer);
  }

  {
    std::string buffer;
    EXPECT_EQ(0, fs.readlink(*i1, &buffer, readlink_mode::unix));
    EXPECT_EQ("subdir/test.txt", buffer);
    EXPECT_EQ(0, fs.readlink(*i2, &buffer, readlink_mode::unix));
    EXPECT_EQ("../subdir/test.txt", buffer);
  }

  // test error case
  {
    std::string buffer;
    EXPECT_EQ(-EINVAL, fs.readlink(*i3, &buffer));
  }

  // also test expected interface
  {
    auto r = fs.readlink(*i1, readlink_mode::unix);
    EXPECT_TRUE(r);
    EXPECT_EQ("subdir/test.txt", *r);
  }

  {
    auto r = fs.readlink(*i3);
    EXPECT_FALSE(r);
    EXPECT_EQ(-EINVAL, r.error());
  }
}

TEST(filesystem, metadata_symlink_unix) {
  test::test_logger lgr;

  auto mm = std::make_shared<mmap>(test_dir / "unixlink.dwarfs");
  filesystem_v2 fs(lgr, mm);

  auto i1 = fs.find("link.txt");
  auto i2 = fs.find("dir/link.txt");
  auto i3 = fs.find("subdir/test.txt");

  ASSERT_TRUE(i1);
  ASSERT_TRUE(i2);
  ASSERT_TRUE(i3);

  EXPECT_TRUE(i1->is_symlink());
  EXPECT_TRUE(i2->is_symlink());
  EXPECT_TRUE(i3->is_regular_file());

  // readlink_mode::preferred (default)
  {
    std::string buf1, buf2;
    EXPECT_EQ(0, fs.readlink(*i1, &buf1));
    EXPECT_EQ(0, fs.readlink(*i2, &buf2));

#if defined(_WIN32)
    EXPECT_EQ("subdir\\test.txt", buf1);
    EXPECT_EQ("..\\subdir\\test.txt", buf2);
#else
    EXPECT_EQ("subdir/test.txt", buf1);
    EXPECT_EQ("../subdir/test.txt", buf2);
#endif
  }

  {
    std::string buffer;
    EXPECT_EQ(0, fs.readlink(*i1, &buffer, readlink_mode::raw));
    EXPECT_EQ("subdir/test.txt", buffer);
    EXPECT_EQ(0, fs.readlink(*i2, &buffer, readlink_mode::raw));
    EXPECT_EQ("../subdir/test.txt", buffer);
  }

  {
    std::string buffer;
    EXPECT_EQ(0, fs.readlink(*i1, &buffer, readlink_mode::unix));
    EXPECT_EQ("subdir/test.txt", buffer);
    EXPECT_EQ(0, fs.readlink(*i2, &buffer, readlink_mode::unix));
    EXPECT_EQ("../subdir/test.txt", buffer);
  }

  // test error case
  {
    std::string buffer;
    EXPECT_EQ(-EINVAL, fs.readlink(*i3, &buffer));
  }

  // also test expected interface
  {
    auto r = fs.readlink(*i1, readlink_mode::unix);
    EXPECT_TRUE(r);
    EXPECT_EQ("subdir/test.txt", *r);
  }

  {
    auto r = fs.readlink(*i3);
    EXPECT_FALSE(r);
    EXPECT_EQ(-EINVAL, r.error());
  }
}
