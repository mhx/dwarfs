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

#include <archive.h>
#include <archive_entry.h>

#include <fmt/format.h>

#include "test_tool_main_tester.h"

using namespace dwarfs::test;
using namespace dwarfs;

#ifndef DWARFS_FILESYSTEM_EXTRACTOR_NO_OPEN_FORMAT
TEST(dwarfsextract_test, mtree) {
  auto t = dwarfsextract_tester::create_with_image();
  ASSERT_EQ(0, t.run({"-i", "image.dwarfs", "-f", "mtree", "--format-options",
                      "mtree:sha256"}))
      << t.err();
  auto out = t.out();
  EXPECT_TRUE(out.starts_with("#mtree")) << out;
  EXPECT_THAT(out, ::testing::HasSubstr("type=dir"));
  EXPECT_THAT(out, ::testing::HasSubstr("type=file"));
  EXPECT_THAT(out, ::testing::HasSubstr("sha256digest="));
}

TEST(dwarfsextract_test, filters) {
  auto t = dwarfsextract_tester::create_with_image();
  ASSERT_EQ(0, t.run({"-i", "image.dwarfs", "-f", "gnutar", "--format-filters",
                      "zstd", "--format-options", "zstd:compression-level=3",
                      "--log-level=debug"}))
      << t.err();

  auto out = t.out();

  auto ar = ::archive_read_new();
  ASSERT_EQ(ARCHIVE_OK,
            ::archive_read_set_format(ar, ARCHIVE_FORMAT_TAR_GNUTAR))
      << ::archive_error_string(ar);
  ASSERT_THAT(::archive_read_append_filter(ar, ARCHIVE_FILTER_ZSTD),
              ::testing::AnyOf(ARCHIVE_OK, ARCHIVE_WARN))
      << ::archive_error_string(ar);
  ASSERT_EQ(ARCHIVE_OK, ::archive_read_open_memory(ar, out.data(), out.size()))
      << ::archive_error_string(ar);

  struct archive_entry* entry;
  int ret = ::archive_read_next_header(ar, &entry);

  EXPECT_EQ(ARCHIVE_OK, ret) << ::archive_error_string(ar);

  EXPECT_EQ(ARCHIVE_OK, ::archive_read_free(ar)) << ::archive_error_string(ar);
}

TEST(dwarfsextract_test, auto_format) {
  auto t = dwarfsextract_tester::create_with_image();
  ASSERT_EQ(0, t.run({"-i", "image.dwarfs", "-f", "auto", "-o", "image.tar"}))
      << t.err();

  auto out = t.fa->get_file("image.tar").value();

  auto ar = ::archive_read_new();
  ASSERT_EQ(ARCHIVE_OK, ::archive_read_support_format_all(ar))
      << ::archive_error_string(ar);
  ASSERT_EQ(ARCHIVE_OK, ::archive_read_open_memory(ar, out.data(), out.size()))
      << ::archive_error_string(ar);

  struct archive_entry* entry;
  int ret = ::archive_read_next_header(ar, &entry);

  EXPECT_EQ(ARCHIVE_OK, ret) << ::archive_error_string(ar);
  auto fmt = ::archive_format(ar);
  EXPECT_EQ(ARCHIVE_FORMAT_TAR, fmt & ARCHIVE_FORMAT_BASE_MASK) << fmt::format(
      "expected TAR ({:08x}), got {:08x}", ARCHIVE_FORMAT_TAR, fmt);

  EXPECT_EQ(ARCHIVE_OK, ::archive_read_free(ar)) << ::archive_error_string(ar);
}

TEST(dwarfsextract_test, auto_format_stdout) {
  auto t = dwarfsextract_tester::create_with_image();
  EXPECT_NE(0, t.run({"-i", "image.dwarfs", "-f", "auto"})) << t.err();
  EXPECT_THAT(t.err(),
              ::testing::HasSubstr("auto format requires output path"));
}

TEST(dwarfsextract_test, auto_format_no_filters) {
  auto t = dwarfsextract_tester::create_with_image();
  EXPECT_NE(0, t.run({"-i", "image.dwarfs", "-f", "auto", "-o", "image.tar",
                      "--format-filters", "zstd"}))
      << t.err();
  EXPECT_THAT(t.err(),
              ::testing::HasSubstr("auto format does not support filters"));
}

TEST(dwarfsextract_test, patterns) {
  auto mkdt = mkdwarfs_tester::create_empty();
  mkdt.add_test_file_tree();
  ASSERT_EQ(0, mkdt.run({"-i", "/", "-o", "-", "--with-devices"}) != 0)
      << mkdt.err();
  auto t = dwarfsextract_tester::create_with_image(mkdt.out());
  ASSERT_EQ(0, t.run({"-i", "image.dwarfs", "-f", "mtree", "**/*.enc",
                      "{dev,etc,lib,var}/[m-ot-z]*"}))
      << t.err();
  auto out = t.out();
  EXPECT_TRUE(out.starts_with("#mtree")) << out;
  std::vector<std::string> const expected{
      "./dev",
      "./dev/tty37",
      "./etc",
      "./etc/netconfig",
      "./usr",
      "./usr/lib64",
      "./usr/lib64/tcl8.6",
      "./usr/lib64/tcl8.6/encoding",
      "./usr/lib64/tcl8.6/encoding/cp950.enc",
      "./usr/lib64/tcl8.6/encoding/iso8859-8.enc",
  };
  auto mtree = test::parse_mtree(out);
  std::vector<std::string> actual;
  for (auto const& entry : mtree) {
    actual.push_back(entry.first);
  }
  EXPECT_EQ(expected, actual);
}

TEST(dwarfsextract_test, stdout_progress_error) {
  auto t = dwarfsextract_tester::create_with_image();
  EXPECT_NE(0,
            t.run({"-i", "image.dwarfs", "-f", "mtree", "--stdout-progress"}))
      << t.err();
  EXPECT_THAT(t.err(), ::testing::HasSubstr(
                           "cannot use --stdout-progress with --output=-"));
}

TEST(dwarfsextract_test, archive_error) {
  auto tgen = mkdwarfs_tester::create_empty();
  tgen.add_root_dir();
  tgen.add_random_file_tree(
      {.avg_size = 32.0, .dimension = 5, .max_name_len = 250});
  ASSERT_EQ(0, tgen.run({"-i", "/", "-l3", "-o", "-"})) << tgen.err();
  auto image = tgen.out();

  auto t = dwarfsextract_tester::create_with_image(image);
  EXPECT_EQ(1, t.run({"-i", "image.dwarfs", "-f", "ustar"})) << t.err();
  EXPECT_THAT(t.err(), ::testing::HasSubstr("archive_error"));
  EXPECT_THAT(t.err(), ::testing::HasSubstr("extraction aborted"));
}

namespace {

struct libarchive_format_def {
  std::string name;
  std::optional<std::string> expected_error{};
  size_t min_size{1000};
};

std::ostream& operator<<(std::ostream& os, libarchive_format_def const& def) {
  return os << def.name;
}

std::vector<libarchive_format_def> const libarchive_formats{
    {"7zip"},
#if 0
    // The AR formats are disabled because they're not meant to support
    // non-regular files.
    //
    // These were removed when `filesystem_extractor` introduced a strict
    // check that `archive_write_data` actually returns that is has written
    // all bytes.

    {"ar"},
    {"arbsd"},
    {"argnu"},
    {"arsvr4"},
#endif
    {"bin"},
    {"bsdtar"},
    {"cd9660"},
    {"cpio"},
    {"gnutar"},
    {"iso"},
    {"iso9660"},
    {"mtree", std::nullopt, 500},
    {"mtree-classic", std::nullopt, 500},
    {"newc"},
    {"odc"},
    {"oldtar"},
    {"pax"},
    {"paxr"},
    {"posix"},
    {"pwb", "symbolic links cannot be represented in the PWB cpio format"},
    {"raw", "Raw format only supports filetype AE_IFREG"},
    {"rpax"},
    {"shar"},
    {"shardump"},
    {"ustar"},
    {"v7tar"},
    {"v7"},
    {"warc", "WARC format cannot archive"},
    {"xar"},
    {"zip"},
};

} // namespace

class dwarfsextract_format_test
    : public testing::TestWithParam<libarchive_format_def> {};

TEST_P(dwarfsextract_format_test, basic) {
  auto fmt = GetParam();
  bool const is_ar = fmt.name.starts_with("ar");
  bool const is_shar = fmt.name.starts_with("shar");
  auto t = dwarfsextract_tester::create_with_image();
  int const expected_exit = fmt.expected_error ? 1 : 0;
  int exit_code =
      t.run({"-i", "image.dwarfs", "-f", fmt.name, "--log-level=debug"});
  if (!fmt.expected_error && exit_code != 0) {
    if (t.err().find("not supported on this platform") != std::string::npos) {
      GTEST_SKIP();
    }
  }
  ASSERT_EQ(expected_exit, exit_code) << t.err();
  if (fmt.expected_error) {
    EXPECT_THAT(t.err(), ::testing::HasSubstr(fmt.expected_error.value()));
    EXPECT_THAT(t.err(), ::testing::HasSubstr("extraction aborted"));
  } else if (!is_shar and !is_ar) {
    auto out = t.out();
    EXPECT_GE(out.size(), fmt.min_size);

    std::set<std::string> paths;
    std::set<std::string> expected_paths{
        "bar.pl",           "baz.pl",   "empty",       "foo.pl",
        "ipsum.txt",        "somedir",  "somedir/bad", "somedir/empty",
        "somedir/ipsum.py", "somelink", "test.pl",
    };

    auto ar = ::archive_read_new();
    ASSERT_EQ(ARCHIVE_OK, ::archive_read_support_format_all(ar))
        << ::archive_error_string(ar);
    ASSERT_EQ(ARCHIVE_OK,
              ::archive_read_open_memory(ar, out.data(), out.size()))
        << ::archive_error_string(ar);

    for (;;) {
      struct archive_entry* entry;
      int ret = ::archive_read_next_header(ar, &entry);
      if (ret == ARCHIVE_EOF) {
        break;
      }
      ASSERT_EQ(ARCHIVE_OK, ret) << ::archive_error_string(ar);
      std::string path{::archive_entry_pathname(entry)};
      if (path != ".") {
        if (path.back() == '/') {
          path.pop_back();
        }
        if (path.starts_with("./")) {
          path.erase(0, 2);
        }
        std::replace(path.begin(), path.end(), '\\', '/');
        EXPECT_TRUE(paths.insert(path).second) << path;
      }
    }

    EXPECT_EQ(ARCHIVE_OK, ::archive_read_free(ar))
        << ::archive_error_string(ar);

    EXPECT_EQ(expected_paths, paths);
  }
}

INSTANTIATE_TEST_SUITE_P(dwarfs, dwarfsextract_format_test,
                         ::testing::ValuesIn(libarchive_formats));

#endif
