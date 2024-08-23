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

#include <folly/Utility.h>

#include <dwarfs/fstypes.h>
#include <dwarfs/mmap.h>
#include <dwarfs/reader/filesystem_options.h>
#include <dwarfs/reader/filesystem_v2.h>

#include "mmap_mock.h"
#include "test_helpers.h"
#include "test_logger.h"

using namespace dwarfs;

namespace fs = std::filesystem;

namespace {

auto test_dir = fs::path(TEST_DATA_DIR).make_preferred();

} // namespace

TEST(filesystem, metadata_symlink_win) {
  test::test_logger lgr;
  test::os_access_mock os;

  auto mm = std::make_shared<mmap>(test_dir / "winlink.dwarfs");
  reader::filesystem_v2 fs(lgr, os, mm);

  auto dev1 = fs.find("link.txt");
  auto dev2 = fs.find("dir/link.txt");
  auto dev3 = fs.find("subdir/test.txt");

  ASSERT_TRUE(dev1);
  ASSERT_TRUE(dev2);
  ASSERT_TRUE(dev3);

  auto i1 = dev1->inode();
  auto i2 = dev2->inode();
  auto i3 = dev3->inode();

  EXPECT_TRUE(i1.is_symlink());
  EXPECT_TRUE(i2.is_symlink());
  EXPECT_TRUE(i3.is_regular_file());

  // readlink_mode::preferred (default)
  {
    auto buf1 = fs.readlink(i1);
    auto buf2 = fs.readlink(i2);

#if defined(_WIN32)
    EXPECT_EQ("subdir\\test.txt", buf1);
    EXPECT_EQ("..\\subdir\\test.txt", buf2);
#else
    EXPECT_EQ("subdir/test.txt", buf1);
    EXPECT_EQ("../subdir/test.txt", buf2);
#endif
  }

  {
    auto buffer = fs.readlink(i1, reader::readlink_mode::raw);
    EXPECT_EQ("subdir\\test.txt", buffer);
    buffer = fs.readlink(i2, reader::readlink_mode::raw);
    EXPECT_EQ("..\\subdir\\test.txt", buffer);
  }

  {
    auto buffer = fs.readlink(i1, reader::readlink_mode::posix);
    EXPECT_EQ("subdir/test.txt", buffer);
    buffer = fs.readlink(i2, reader::readlink_mode::posix);
    EXPECT_EQ("../subdir/test.txt", buffer);
  }

  {
    std::error_code ec;
    auto r = fs.readlink(i1, reader::readlink_mode::posix, ec);
    EXPECT_FALSE(ec);
    EXPECT_EQ("subdir/test.txt", r);
  }

  {
    std::error_code ec;
    auto r = fs.readlink(i3, ec);
    EXPECT_TRUE(ec);
    EXPECT_EQ(EINVAL, ec.value());
  }

  // also test throwing interface
  {
    auto r = fs.readlink(i1, reader::readlink_mode::posix);
    EXPECT_EQ("subdir/test.txt", r);
  }

  {
    EXPECT_THAT([&] { fs.readlink(i3); },
                ::testing::Throws<std::system_error>());
  }
}

TEST(filesystem, metadata_symlink_unix) {
  test::test_logger lgr;
  test::os_access_mock os;

  auto mm = std::make_shared<mmap>(test_dir / "unixlink.dwarfs");
  reader::filesystem_v2 fs(lgr, os, mm);

  auto dev1 = fs.find("link.txt");
  auto dev2 = fs.find("dir/link.txt");
  auto dev3 = fs.find("subdir/test.txt");

  ASSERT_TRUE(dev1);
  ASSERT_TRUE(dev2);
  ASSERT_TRUE(dev3);

  auto i1 = dev1->inode();
  auto i2 = dev2->inode();
  auto i3 = dev3->inode();

  EXPECT_TRUE(i1.is_symlink());
  EXPECT_TRUE(i2.is_symlink());
  EXPECT_TRUE(i3.is_regular_file());

  // readlink_mode::preferred (default)
  {
    auto buf1 = fs.readlink(i1);
    auto buf2 = fs.readlink(i2);

#if defined(_WIN32)
    EXPECT_EQ("subdir\\test.txt", buf1);
    EXPECT_EQ("..\\subdir\\test.txt", buf2);
#else
    EXPECT_EQ("subdir/test.txt", buf1);
    EXPECT_EQ("../subdir/test.txt", buf2);
#endif
  }

  {
    auto buffer = fs.readlink(i1, reader::readlink_mode::raw);
    EXPECT_EQ("subdir/test.txt", buffer);
    buffer = fs.readlink(i2, reader::readlink_mode::raw);
    EXPECT_EQ("../subdir/test.txt", buffer);
  }

  {
    auto buffer = fs.readlink(i1, reader::readlink_mode::posix);
    EXPECT_EQ("subdir/test.txt", buffer);
    buffer = fs.readlink(i2, reader::readlink_mode::posix);
    EXPECT_EQ("../subdir/test.txt", buffer);
  }

  {
    std::error_code ec;
    auto r = fs.readlink(i1, reader::readlink_mode::posix, ec);
    EXPECT_FALSE(ec);
    EXPECT_EQ("subdir/test.txt", r);
  }

  {
    std::error_code ec;
    auto r = fs.readlink(i3, ec);
    EXPECT_TRUE(ec);
    EXPECT_EQ(EINVAL, ec.value());
  }
}

namespace {

std::string valid_v1_header() {
  section_header hdr;
  hdr.type = section_type::BLOCK;
  hdr.compression = compression_type_v1::NONE;
  hdr.length = 1;
  std::string buf;
  buf.resize(8 + sizeof(hdr));
  std::memcpy(buf.data(), "DWARFS\x02\x01", 8);
  std::memcpy(buf.data() + 8, &hdr, sizeof(hdr));
  return buf;
}

std::string valid_v2_header(uint32_t section_number = 0) {
  section_header_v2 hdr;
  std::memset(&hdr, 0, sizeof(hdr));
  std::memcpy(hdr.magic, "DWARFS", 6);
  hdr.major = 2;
  hdr.minor = 3;
  hdr.number = section_number;
  hdr.type = folly::to_underlying(section_type::BLOCK);
  hdr.compression = folly::to_underlying(compression_type::NONE);
  hdr.length = 1;
  std::string buf;
  buf.resize(sizeof(hdr));
  std::memcpy(buf.data(), &hdr, sizeof(hdr));
  return buf;
}

} // namespace

TEST(filesystem, find_image_offset) {
  DWARFS_SLOW_TEST();

  test::test_logger lgr;
  test::os_access_mock os;

  auto make_fs =
      [&](std::string data,
          reader::filesystem_options const& opt = {
              .image_offset = reader::filesystem_options::IMAGE_OFFSET_AUTO}) {
        return reader::filesystem_v2(
            lgr, os, std::make_shared<test::mmap_mock>(std::move(data)), opt);
      };

  auto throws_rt_error = [](auto substr) {
    return ::testing::ThrowsMessage<runtime_error>(
        ::testing::HasSubstr(substr));
  };

  auto throws_no_fs_found = throws_rt_error("no filesystem found");
  auto throws_no_schema = throws_rt_error("no metadata schema found");

  std::string valid_fs;
  {
    mmap mm(test_dir / "unixlink.dwarfs");
    auto data = mm.span<char>();
    valid_fs.assign(data.data(), data.size());
  }
  auto v1_header = valid_v1_header();
  auto v2_header = valid_v2_header();
  auto prefix = "DWARFS\x02\x02 DWARFS\x02\x02 xxxxxxxxxxxxxxxxxxxxDWARFS\x02";

  EXPECT_NO_THROW(make_fs(valid_fs));
  EXPECT_NO_THROW(make_fs(prefix + valid_fs));

  EXPECT_THAT([&] { make_fs("dwarfs"); }, throws_no_fs_found);
  EXPECT_THAT([&] { make_fs(v1_header + "X"); }, throws_no_schema);
  EXPECT_THAT([&] { make_fs(v2_header + "X"); }, throws_no_fs_found);
  EXPECT_THAT([&] { make_fs(v2_header + "X" + valid_v2_header(1) + "X"); },
              throws_no_schema);
  EXPECT_THAT([&] { make_fs(prefix); }, throws_no_fs_found);

  for (size_t len = 0; len < valid_fs.size() - 1; ++len) {
    auto truncated = valid_fs.substr(0, len);
    EXPECT_THAT([&] { make_fs(truncated); }, ::testing::Throws<runtime_error>())
        << "len=" << len;
    EXPECT_THAT([&] { make_fs(prefix + truncated); },
                ::testing::Throws<runtime_error>())
        << "len=" << len;
  }
}
