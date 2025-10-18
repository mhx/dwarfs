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

#include <filesystem>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <fmt/format.h>

#include <folly/Utility.h>

#include <dwarfs/file_util.h>
#include <dwarfs/fstypes.h>
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

  auto mm = test::make_real_file_view(test_dir / "winlink.dwarfs");
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

  auto mm = test::make_real_file_view(test_dir / "unixlink.dwarfs");
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
  section_header hdr{};
  hdr.type = folly::to_underlying(section_type::BLOCK);
  hdr.compression = compression_type_v1::NONE;
  hdr.length = 1;
  std::string buf;
  buf.resize(8 + sizeof(hdr));
  std::memcpy(buf.data(), "DWARFS\x02\x01", 8);
  std::memcpy(buf.data() + 8, &hdr, sizeof(hdr));
  return buf;
}

std::string valid_v2_header(uint32_t section_number = 0) {
  section_header_v2 hdr{};
  std::memcpy(hdr.magic.data(), "DWARFS", 6);
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
            lgr, os, test::make_mock_file_view(std::move(data)), opt);
      };

  auto throws_rt_error = [](auto substr) {
    return ::testing::ThrowsMessage<runtime_error>(
        ::testing::HasSubstr(substr));
  };

  auto throws_no_fs_found = throws_rt_error("no filesystem found");
  auto throws_no_schema = throws_rt_error("no metadata schema found");

  std::string valid_fs;
  {
    auto mm = test::make_real_file_view(test_dir / "unixlink.dwarfs");
    auto data = mm.raw_bytes<char>();
    valid_fs.assign(data.data(), data.size());
  }
  auto v1_header = valid_v1_header();
  auto v2_header = valid_v2_header();
  auto prefix = "DWARFS\x02\x02 DWARFS\x02\x02 xxxxxxxxxxxxxxxxxxxxDWARFS\x02";

  EXPECT_NO_THROW(make_fs(valid_fs));
  EXPECT_NO_THROW(make_fs(prefix + valid_fs));

  EXPECT_THAT([&] { make_fs("dwarfs"); }, throws_no_fs_found);
  EXPECT_THAT([&] { make_fs(v1_header + "X"); }, throws_no_fs_found);
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

TEST(filesystem, find_image_offset_v1) {
  test::test_logger lgr;
  test::os_access_mock os;
  auto const data = read_file(test_dir / "compat" / "compat-v0.2.0.dwarfs");

  EXPECT_NO_THROW(
      reader::filesystem_v2(lgr, os, test::make_mock_file_view(data)));

  auto const truncated = data.substr(0, 16);
  auto mm = test::make_mock_file_view(truncated);

  EXPECT_THAT([&] { reader::filesystem_v2(lgr, os, mm); },
              ::testing::ThrowsMessage<runtime_error>(
                  ::testing::HasSubstr("truncated section data")));

  EXPECT_THAT(
      [&] {
        reader::filesystem_v2(
            lgr, os, mm,
            {.image_offset = reader::filesystem_options::IMAGE_OFFSET_AUTO});
      },
      ::testing::ThrowsMessage<runtime_error>(
          ::testing::HasSubstr("no filesystem found")));

  auto const truncated2 = std::string(13, 'x') + truncated;

  EXPECT_THAT(
      [&] {
        reader::filesystem_v2(
            lgr, os, mm,
            {.image_offset = reader::filesystem_options::IMAGE_OFFSET_AUTO});
      },
      ::testing::ThrowsMessage<runtime_error>(
          ::testing::HasSubstr("no filesystem found")));
}

TEST(filesystem, check_valid_image) {
  test::test_logger lgr;
  test::os_access_mock os;
  auto const data = read_file(test_dir / "compat" / "compat-v0.9.10.dwarfs");

  EXPECT_NO_THROW(
      reader::filesystem_v2(lgr, os, test::make_mock_file_view(data)));

  EXPECT_THAT(
      [&] {
        reader::filesystem_v2(lgr, os, test::make_mock_file_view("DWARFS"));
      },
      ::testing::ThrowsMessage<runtime_error>(
          ::testing::HasSubstr("filesystem image too small")));

  {
    auto tmp = data;
    tmp[6] = 0x01; // unsupported major version

    EXPECT_THAT(
        [&] { reader::filesystem_v2(lgr, os, test::make_mock_file_view(tmp)); },
        ::testing::ThrowsMessage<runtime_error>(
            ::testing::HasSubstr("unsupported major version")));
  }

  {
    auto tmp = data;
    tmp[7] = MINOR_VERSION_ACCEPTED + 1; // unsupported minor version

    EXPECT_THAT(
        [&] { reader::filesystem_v2(lgr, os, test::make_mock_file_view(tmp)); },
        ::testing::ThrowsMessage<runtime_error>(
            ::testing::HasSubstr("unsupported minor version")));
  }
}

TEST(filesytem, check_section_index) {
  test::test_logger lgr;
  test::os_access_mock os;
  auto const data = read_file(test_dir / "compat" / "compat-v0.9.10.dwarfs");

  EXPECT_NO_THROW(
      reader::filesystem_v2(lgr, os, test::make_mock_file_view(data)));

  auto ii = data.rfind("DWARFS");
  ASSERT_NE(ii, std::string::npos);

  auto const data_noindex = data.substr(0, ii);
  auto const index = data.substr(ii);

  section_header_v2 sh{};
  std::memcpy(&sh, index.data(), sizeof(sh));

  ASSERT_EQ((index.size() - sizeof(sh)) % sizeof(uint64le_t), 0u);
  size_t const num_offsets = (index.size() - sizeof(sh)) / sizeof(uint64le_t);
  std::vector<uint64le_t> offsets(num_offsets);
  std::memcpy(offsets.data(), index.data() + sizeof(sh),
              index.size() - sizeof(sh));

  auto make_fs = [&](bool invalid_checksum = false,
                     bool corrupt_length = false) {
    auto tmp{sh};

    size_t offlen = sizeof(uint64_t) * offsets.size();
    auto offptr = reinterpret_cast<uint8_t*>(offsets.data());

    if (corrupt_length) {
      offlen -= 1;
      offptr += 1;
    }

    tmp.length = offlen;

    if (invalid_checksum) {
      tmp.xxh3_64 = 0;
    } else {
      checksum xxh(checksum::xxh3_64);
      xxh.update(&tmp.number, sizeof(section_header_v2) -
                                  offsetof(section_header_v2, number));
      xxh.update(offptr, offlen);
      EXPECT_TRUE(xxh.finalize(&tmp.xxh3_64));
    }

    std::string buf(sizeof(tmp) + offlen, '\0');
    std::memcpy(buf.data(), &tmp, sizeof(tmp));
    std::memcpy(buf.data() + sizeof(tmp), offptr, offlen);

    return reader::filesystem_v2(lgr, os,
                                 test::make_mock_file_view(data_noindex + buf));
  };

  EXPECT_TRUE(make_fs().has_valid_section_index());

  offsets[0] = offsets[0] + 1; // first offset *must* be zero

  EXPECT_FALSE(make_fs().has_valid_section_index());

  offsets[0] = offsets[0] - 1;       // undo
  std::swap(offsets[1], offsets[2]); // offsets must be sorted

  EXPECT_FALSE(make_fs().has_valid_section_index());

  std::swap(offsets[1], offsets[2]); // undo
  sh.type = 4;                       // invalid section type

  EXPECT_FALSE(make_fs().has_valid_section_index());

  sh.type = static_cast<uint16_t>(section_type::SECTION_INDEX);
  sh.compression =
      static_cast<uint16_t>(compression_type::ZSTD); // must be NONE

  EXPECT_FALSE(make_fs().has_valid_section_index());

  sh.compression = static_cast<uint16_t>(compression_type::NONE); // undo

  EXPECT_FALSE(make_fs(false, true).has_valid_section_index());

  auto tmp_back = offsets.back();
  offsets.back() = (tmp_back & ((UINT64_C(1) << 48) - 1)); // invalid type

  EXPECT_FALSE(make_fs().has_valid_section_index());

  offsets.back() = tmp_back + 1000; // must be within image

  EXPECT_FALSE(make_fs().has_valid_section_index());

  offsets.back() = tmp_back; // undo

  EXPECT_FALSE(make_fs(true).has_valid_section_index());

  offsets.erase(std::next(offsets.begin()),
                std::prev(offsets.end())); // too few offsets
  EXPECT_EQ(2u, offsets.size());

  EXPECT_FALSE(make_fs().has_valid_section_index());
}

TEST(filesystem, future_features) {
  test::test_logger lgr;
  test::os_access_mock os;
  auto const data = read_file(test_dir / "future-features.dwarfs");

  EXPECT_THAT(
      [&] { reader::filesystem_v2(lgr, os, test::make_mock_file_view(data)); },
      ::testing::ThrowsMessage<runtime_error>(::testing::HasSubstr(
          "file system uses the following features unsupported by this build: "
          "this-feature-will-never-exist")));
}
