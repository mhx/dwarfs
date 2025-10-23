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

#include <dwarfs/file_stat.h>
#include <dwarfs/file_util.h>

#include <dwarfs/internal/file_status_conv.h>

namespace fs = std::filesystem;

TEST(file_utils, file_status_conversion) {
  using namespace dwarfs::internal;
  using fs::file_type;
  using fs::perms;

  EXPECT_THAT(
      [] { return file_mode_to_status(0); },
      testing::ThrowsMessage<std::runtime_error>("invalid file mode: 0x0000"));

  auto status = file_mode_to_status(0140755);
  EXPECT_EQ(status.type(), file_type::socket);
  EXPECT_EQ(status.permissions(), perms::owner_all | perms::group_read |
                                      perms::group_exec | perms::others_read |
                                      perms::others_exec);
  EXPECT_EQ(file_status_to_mode(status), 0140755);

  status = file_mode_to_status(0120644);
  EXPECT_EQ(status.type(), file_type::symlink);
  EXPECT_EQ(status.permissions(), perms::owner_read | perms::owner_write |
                                      perms::group_read | perms::others_read);
  EXPECT_EQ(file_status_to_mode(status), 0120644);

  status = file_mode_to_status(0104400);
  EXPECT_EQ(status.type(), file_type::regular);
  EXPECT_EQ(status.permissions(), perms::set_uid | perms::owner_read);
  EXPECT_EQ(file_status_to_mode(status), 0104400);

  status = file_mode_to_status(0060004);
  EXPECT_EQ(status.type(), file_type::block);
  EXPECT_EQ(status.permissions(), perms::others_read);
  EXPECT_EQ(file_status_to_mode(status), 0060004);

  status = file_mode_to_status(0042010);
  EXPECT_EQ(status.type(), file_type::directory);
  EXPECT_EQ(status.permissions(), perms::set_gid | perms::group_exec);
  EXPECT_EQ(file_status_to_mode(status), 0042010);

  status = file_mode_to_status(0021007);
  EXPECT_EQ(status.type(), file_type::character);
  EXPECT_EQ(status.permissions(), perms::sticky_bit | perms::others_all);
  EXPECT_EQ(file_status_to_mode(status), 0021007);

  status = file_mode_to_status(0017777);
  EXPECT_EQ(status.type(), file_type::fifo);
  EXPECT_EQ(status.permissions(),
            perms::sticky_bit | perms::set_uid | perms::set_gid | perms::all);
  EXPECT_EQ(file_status_to_mode(status), 0017777);

  status.type(file_type::none);
  EXPECT_THAT([&] { file_status_to_mode(status); },
              testing::ThrowsMessage<std::runtime_error>(
                  testing::HasSubstr("invalid file type: ")));
}

TEST(file_utils, file_stat_nonexistent) {
  using namespace dwarfs;

  file_stat st("somenonexistentfile");

  EXPECT_THAT([&] { st.ensure_valid(file_stat::mode_valid); },
              testing::Throws<std::system_error>());
}

TEST(file_utils, file_stat) {
  using namespace dwarfs;

  file_stat st;

  EXPECT_THAT([&] { st.ensure_valid(file_stat::ino_valid); },
              testing::ThrowsMessage<dwarfs::runtime_error>(
                  testing::HasSubstr("missing stat fields:")));

  EXPECT_THAT([&] { st.set_permissions(0755); },
              testing::ThrowsMessage<dwarfs::runtime_error>(
                  testing::HasSubstr("missing stat fields:")));

  EXPECT_THAT([&] { st.status(); },
              testing::ThrowsMessage<dwarfs::runtime_error>(
                  testing::HasSubstr("missing stat fields:")));

  st.set_mode(0100644);

  EXPECT_NO_THROW(st.set_permissions(0755));
  EXPECT_NO_THROW(st.ensure_valid(file_stat::mode_valid));

  auto status = st.status();
  EXPECT_EQ(status.permissions(), fs::perms::owner_all | fs::perms::group_read |
                                      fs::perms::group_exec |
                                      fs::perms::others_read |
                                      fs::perms::others_exec);

  EXPECT_TRUE(st.is_regular_file());
  EXPECT_FALSE(st.is_directory());
  EXPECT_FALSE(st.is_symlink());

  st.set_mode(0040755);

  EXPECT_FALSE(st.is_regular_file());
  EXPECT_TRUE(st.is_directory());
  EXPECT_FALSE(st.is_symlink());

  st.set_mode(0120644);

  EXPECT_FALSE(st.is_regular_file());
  EXPECT_FALSE(st.is_directory());
  EXPECT_TRUE(st.is_symlink());

  EXPECT_THAT([&] { st.dev(); },
              testing::ThrowsMessage<dwarfs::runtime_error>(
                  testing::HasSubstr("missing stat fields:")));

  EXPECT_EQ(st.dev_unchecked(), 0);

  st.set_dev(1234);
  EXPECT_EQ(st.dev(), 1234);

  st.set_blksize(4096);
  st.set_blocks(8);

  EXPECT_NO_THROW(st.ensure_valid(file_stat::blksize_valid |
                                  file_stat::blocks_valid |
                                  file_stat::dev_valid));

  EXPECT_EQ(st.blksize(), 4096);
  EXPECT_EQ(st.blocks(), 8);

  EXPECT_EQ(file_stat::mode_string(0100644), "----rw-r--r--");
  EXPECT_EQ(file_stat::mode_string(0120644), "---lrw-r--r--");
  EXPECT_EQ(file_stat::mode_string(0140644), "---srw-r--r--");

  EXPECT_THAT([&] { file_stat::mode_string(0110000); },
              testing::ThrowsMessage<dwarfs::runtime_error>(
                  testing::HasSubstr("unknown file type: 0x9000")));
}

TEST(file_utils, file_stat_symlink) {
  using namespace dwarfs;

  temporary_directory td("dwarfs");

  write_file(td.path() / "target_file", "Hello, this is a long string!\n");
  fs::copy(td.path() / "target_file", td.path() / u8"我爱你.txt");

  fs::create_symlink("target_file", td.path() / "link_to_target");
  fs::create_symlink(u8"我爱你.txt", td.path() / "link_to_unicode");

  {
    file_stat st(td.path() / "link_to_target");
    EXPECT_TRUE(st.is_symlink());
    EXPECT_EQ(11, st.size());
  }

  {
    file_stat st(td.path() / "link_to_unicode");
    EXPECT_TRUE(st.is_symlink());
    EXPECT_EQ(13, st.size());
  }
}
