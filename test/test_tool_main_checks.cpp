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

#include <string>

#include <gtest/gtest.h>

#include <dwarfs/reader/metadata_types.h>

#include "test_tool_main_checks.h"

namespace dwarfs::test {

namespace {

template <typename T, typename U>
void expect_opt_eq(std::optional<T> const& expected, U const& actual,
                   char const* what) {
  if (expected) {
    EXPECT_EQ(*expected, actual) << what;
  }
}

void expect_type(reader::inode_view const& iv, posix_file_type::value type) {
  switch (type) {
  case posix_file_type::regular:
    EXPECT_TRUE(iv.is_regular_file());
    break;

  case posix_file_type::directory:
    EXPECT_TRUE(iv.is_directory());
    break;

  case posix_file_type::symlink:
    EXPECT_TRUE(iv.is_symlink());
    break;

  default:
    ADD_FAILURE() << "unsupported expected file type: "
                  << static_cast<int>(type);
    break;
  }
}

} // namespace

void expect_attrs(reader::filesystem_v2 const& fs, std::string_view path,
                  expected_attrs const& expected) {
  SCOPED_TRACE(path);

  auto const dev = fs.find(path);
  ASSERT_TRUE(dev);

  auto const iv = dev->inode();

  if (expected.type) {
    expect_type(iv, *expected.type);
  }

  auto const st = fs.getattr(iv);

  expect_opt_eq(expected.size, st.size(), "size");
  expect_opt_eq(expected.allocated_size, st.allocated_size(), "allocated_size");
  expect_opt_eq(expected.blocks, st.blocks(), "blocks");
  expect_opt_eq(expected.atime, st.atime(), "atime");
  expect_opt_eq(expected.mtime, st.mtime(), "mtime");
  expect_opt_eq(expected.ctime, st.ctime(), "ctime");
  expect_opt_eq(expected.atimespec, st.atimespec(), "atimespec");
  expect_opt_eq(expected.mtimespec, st.mtimespec(), "mtimespec");
  expect_opt_eq(expected.ctimespec, st.ctimespec(), "ctimespec");
  expect_opt_eq(expected.uid, st.uid(), "uid");
  expect_opt_eq(expected.gid, st.gid(), "gid");
  expect_opt_eq(expected.permissions, st.permissions(), "permissions");
  expect_opt_eq(expected.nlink, st.nlink(), "nlink");
}

void expect_attrs(reader::filesystem_v2 const& fs,
                  std::initializer_list<expected_entry> entries) {
  for (auto const& entry : entries) {
    ASSERT_NO_FATAL_FAILURE(expect_attrs(fs, entry.path, entry.attrs));
  }
}

void expect_same_inode(reader::filesystem_v2 const& fs, std::string_view a,
                       std::string_view b) {
  SCOPED_TRACE(std::string{a} + " <-> " + std::string{b});

  auto const dev_a = fs.find(a);
  auto const dev_b = fs.find(b);

  ASSERT_TRUE(dev_a);
  ASSERT_TRUE(dev_b);

  EXPECT_EQ(fs.getattr(dev_a->inode()).ino(), fs.getattr(dev_b->inode()).ino());
}

} // namespace dwarfs::test
