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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <dwarfs/internal/file_status_conv.h>

using namespace dwarfs::internal;
namespace fs = std::filesystem;

TEST(file_utils, file_status_conversion) {
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
