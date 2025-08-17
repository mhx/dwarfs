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
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <fstream>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <dwarfs/file_util.h>
#include <dwarfs/xattr.h>

TEST(xattr_test, portable_xattr) {
#ifdef _WIN32
  GTEST_SKIP() << "Extended attributes are not fully implemented on Windows";
#else
  dwarfs::temporary_directory td("dwarfs");
  auto const path = td.path() / "testfile";
  auto const non_existant_path = td.path() / "non_existant_testfile";
  {
    std::ofstream ofs{path.string()};
    ASSERT_TRUE(ofs.is_open());
    ofs << "test content";
    ofs.close();
  }
  std::string const xattr_name = "user.dwarfs_test_attr";
  std::string const non_existant_xattr_name = "user.dwarfs_non_existant_attr";
  std::string const xattr_value = "dwarfs test value";

  std::error_code ec;
  dwarfs::listxattr(path, ec);
  if (ec == std::errc::operation_not_supported) {
    GTEST_SKIP() << "Extended attributes not supported on this filesystem";
  } else if (ec) {
    FAIL() << "Unexpected error listing extended attributes: " << ec.message();
  }

  EXPECT_THAT(dwarfs::listxattr(path),
              ::testing::Not(::testing::Contains(xattr_name)));

  EXPECT_THAT([&] { dwarfs::getxattr(path, non_existant_xattr_name); },
              ::testing::Throws<std::system_error>());

  EXPECT_THAT([&] { dwarfs::getxattr(non_existant_path, xattr_name); },
              ::testing::Throws<std::system_error>());

  dwarfs::setxattr(path, xattr_name, xattr_value);

  EXPECT_THAT(
      [&] { dwarfs::setxattr(non_existant_path, xattr_name, xattr_value); },
      ::testing::Throws<std::system_error>());

  EXPECT_THAT(dwarfs::listxattr(path), ::testing::Contains(xattr_name));

  EXPECT_EQ(dwarfs::getxattr(path, xattr_name), xattr_value);

  EXPECT_THAT([&] { dwarfs::removexattr(path, non_existant_xattr_name); },
              ::testing::Throws<std::system_error>());

  EXPECT_THAT([&] { dwarfs::removexattr(non_existant_path, xattr_name); },
              ::testing::Throws<std::system_error>());

  dwarfs::removexattr(path, xattr_name);

  EXPECT_EQ(dwarfs::getxattr(path, xattr_name, ec), std::string{});
  EXPECT_TRUE(ec);
#endif
}
