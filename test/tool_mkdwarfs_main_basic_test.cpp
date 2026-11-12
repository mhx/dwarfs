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

#include <algorithm>
#include <chrono>
#include <ctime>

#include <gmock/gmock.h>

#include <fmt/format.h>

#ifdef DWARFS_USE_JEMALLOC
#include <jemalloc/jemalloc.h>
#endif

#include <dwarfs/container/sorted_array_map.h>
#include <dwarfs/conv.h>
#include <dwarfs/file_util.h>
#include <dwarfs/reader/fsinfo_options.h>
#include <dwarfs/string.h>

#include "test_tool_main_tester.h"

using namespace dwarfs::test;
using namespace dwarfs;

namespace fs = std::filesystem;

class mkdwarfs_input_list_test
    : public testing::TestWithParam<std::tuple<input_mode, path_type>> {};

TEST(mkdwarfs_test, positional_sources) {
  auto t = mkdwarfs_tester::create_empty();
  t.os->add_dir("/");
  t.os->add_dir("/one");
  t.os->add_dir("/one/common");
  t.os->add_file("/one/common/a", "a");
  t.os->add_file("/one/common/shared", "first");
  t.os->add_dir("/two");
  t.os->add_dir("/two/common");
  t.os->add_file("/two/common/b", "b");
  t.os->add_file("/two/common/shared", "second");
  t.os->add_dir("/named");
  t.os->add_file("/named/c", "c");
  t.os->add_file("/standalone", "text");

  ASSERT_EQ(0, t.run({"-o", "test.dwarfs", "-l", "1", "/one/", "/two/",
                      "/named", "/standalone"}))
      << t.err();

  auto fs = t.fs_from_file("test.dwarfs");
  EXPECT_EQ(
      (std::set<fs::path>{"", "common", "common/a", "common/b", "common/shared",
                          "named", "named/c", "standalone"}),
      get_all_fs_paths(fs));
  EXPECT_THAT(t.err(), ::testing::HasSubstr("merging directory"));
  EXPECT_THAT(t.err(), ::testing::HasSubstr("duplicate entry"));
}

TEST(mkdwarfs_test, positional_sources_debug_filter_omits_collisions) {
  auto t = mkdwarfs_tester::create_empty();
  t.os->add_dir("/");
  t.os->add_dir("/one");
  t.os->add_dir("/one/common");
  t.os->add_file("/one/common/a", "a");
  t.os->add_file("/one/common/shared", "first");
  t.os->add_dir("/two");
  t.os->add_dir("/two/common");
  t.os->add_file("/two/common/b", "b");
  t.os->add_file("/two/common/shared", "second");

  ASSERT_EQ(0, t.run({"--debug-filter=included-files", "/one/", "/two/"}))
      << t.err();

  // Only three files end up in the image. The second `shared` is dropped
  // as a duplicate and must not be reported as included.
  auto const out = t.out();
  EXPECT_EQ(3, std::ranges::count(out, '\n')) << out;
  EXPECT_THAT(out, ::testing::Not(::testing::HasSubstr("/two/common/shared")))
      << out;
  EXPECT_THAT(out, ::testing::HasSubstr("/two/common/b")) << out;
}

TEST(mkdwarfs_test, positional_sources_debug_filter_all_reports_merged_once) {
  auto t = mkdwarfs_tester::create_empty();
  t.os->add_dir("/");
  t.os->add_dir("/one");
  t.os->add_dir("/one/common");
  t.os->add_file("/one/common/a", "a");
  t.os->add_dir("/two");
  t.os->add_dir("/two/common");
  t.os->add_file("/two/common/b", "b");

  ASSERT_EQ(0, t.run({"--debug-filter=all", "/one/", "/two/"})) << t.err();

  // The merged directory is reported once (from the first source), and the
  // entries merged into it from the second source are still reported.
  auto const out = t.out();
  EXPECT_EQ(3, std::ranges::count(out, '\n')) << out;
  EXPECT_THAT(out, ::testing::HasSubstr("+ /one/common/\n")) << out;
  EXPECT_THAT(out, ::testing::Not(::testing::HasSubstr("/two/common/\n")))
      << out;
  EXPECT_THAT(out, ::testing::HasSubstr("+ /two/common/b\n")) << out;
}

namespace {

void add_colliding_sources(mkdwarfs_tester& t) {
  t.os->add_dir("/");
  t.os->add_dir("/one");
  t.os->add_dir("/one/common");
  t.os->add_file("/one/common/a", "a");
  t.os->add_file("/one/common/shared", "first");
  t.os->add_dir("/two");
  t.os->add_dir("/two/common");
  t.os->add_file("/two/common/b", "b");
  t.os->add_file("/two/common/shared", "second");
}

} // namespace

TEST(mkdwarfs_test, positional_sources_order_decides_winner) {
  for (auto const& [first, second, expected] :
       {std::tuple{"/one/", "/two/", "first"},
        std::tuple{"/two/", "/one/", "second"}}) {
    SCOPED_TRACE(
        fmt::format("first={} second={} expected={}", first, second, expected));

    auto t = mkdwarfs_tester::create_empty();
    add_colliding_sources(t);

    ASSERT_EQ(0, t.run({"-o", "test.dwarfs", "-l1", first, second})) << t.err();

    auto fs = t.fs_from_file("test.dwarfs");
    auto shared = fs.find("/common/shared");

    ASSERT_TRUE(shared);
    EXPECT_EQ(std::string_view{expected},
              fs.read_string(fs.open(shared->inode())));
  }
}

TEST(mkdwarfs_test, positional_sources_filter_matches_logical_paths) {
  auto t = mkdwarfs_tester::create_empty();
  add_colliding_sources(t);

  // The rule refers to the *logical* path, so it must remove `shared`
  ASSERT_EQ(0, t.run({"-o", "test.dwarfs", "-l1", "-F", "- /common/shared",
                      "--", "/one/", "/two/"}))
      << t.err();

  auto fs = t.fs_from_file("test.dwarfs");

  EXPECT_EQ((std::set<fs::path>{"", "common", "common/a", "common/b"}),
            get_all_fs_paths(fs));
}

TEST(mkdwarfs_test, positional_sources_later_dir_does_not_replace_file) {
  auto t = mkdwarfs_tester::create_empty();
  t.os->add_dir("/");
  t.os->add_dir("/one");
  t.os->add_file("/one/x", "file");
  t.os->add_dir("/two");
  t.os->add_dir("/two/x");
  t.os->add_file("/two/x/child", "child");

  ASSERT_EQ(0, t.run({"-o", "test.dwarfs", "-l1", "/one/", "/two/"}))
      << t.err();

  auto fs = t.fs_from_file("test.dwarfs");

  EXPECT_EQ((std::set<fs::path>{"", "x"}), get_all_fs_paths(fs));

  auto x = fs.find("/x");

  ASSERT_TRUE(x);
  EXPECT_EQ(posix_file_type::regular, fs.getattr(x->inode()).type());
  EXPECT_THAT(t.err(), ::testing::HasSubstr("duplicate entry '/x/'"));
}

TEST(mkdwarfs_test, positional_sources_symlink_is_not_merged_with_dir) {
  auto t = mkdwarfs_tester::create_empty();
  t.os->add_dir("/");
  t.os->add_dir("/one");
  t.os->add_dir("/one/real");
  t.os->add_file("/one/real/file", "file");
  t.os->add("/one/x",
            {900, posix_file_type::symlink | 0777, 1, 1000, 100, 4, kNoRdev},
            std::string{"real"});
  t.os->add_dir("/two");
  t.os->add_dir("/two/x");
  t.os->add_file("/two/x/child", "child");

  ASSERT_EQ(0, t.run({"-o", "test.dwarfs", "-l1", "/one/", "/two/"}))
      << t.err();

  auto fs = t.fs_from_file("test.dwarfs");

  EXPECT_EQ((std::set<fs::path>{"", "real", "real/file", "x"}),
            get_all_fs_paths(fs));

  auto x = fs.find("/x");

  ASSERT_TRUE(x);
  EXPECT_EQ(posix_file_type::symlink, fs.getattr(x->inode()).type());
}

TEST(mkdwarfs_test, positional_named_symlink_is_stored_as_symlink) {
  auto t = mkdwarfs_tester::create_empty();
  t.os->add_dir("/");
  t.os->add_dir("/data");
  t.os->add_dir("/data/real");
  t.os->add_file("/data/real/file", "file");
  t.os->add("/data/link",
            {900, posix_file_type::symlink | 0777, 1, 1000, 100, 4, kNoRdev},
            std::string{"real"});

  ASSERT_EQ(0, t.run({"-o", "test.dwarfs", "-l1", "/data/link"})) << t.err();

  auto fs = t.fs_from_file("test.dwarfs");

  EXPECT_EQ((std::set<fs::path>{"", "link"}), get_all_fs_paths(fs));

  auto link = fs.find("/link");

  ASSERT_TRUE(link);
  EXPECT_EQ(posix_file_type::symlink, fs.getattr(link->inode()).type());
  std::error_code ec;
  EXPECT_EQ("real", fs.readlink(link->inode(), ec));
  EXPECT_FALSE(ec);
}

TEST(mkdwarfs_test, positional_named_dir_merges_into_contents_dir) {
  auto t = mkdwarfs_tester::create_empty();
  t.os->add_dir("/");
  t.os->add_dir("/a");
  t.os->add_dir("/a/named");
  t.os->add_file("/a/named/x", "xx");
  t.os->add_dir("/b");
  t.os->add_dir("/b/named");
  t.os->add_dir("/b/named/sub");
  t.os->add_file("/b/named/sub/y", "yyyy");

  ASSERT_EQ(0, t.run({"-o", "test.dwarfs", "-l1", "/a/", "/b/named"}))
      << t.err();

  auto fs = t.fs_from_file("test.dwarfs");

  EXPECT_EQ(
      (std::set<fs::path>{"", "named", "named/x", "named/sub", "named/sub/y"}),
      get_all_fs_paths(fs));

  auto y = fs.find("/named/sub/y");

  ASSERT_TRUE(y);
  EXPECT_EQ(4, fs.getattr(y->inode()).size());
  EXPECT_EQ("yyyy", fs.read_string(fs.open(y->inode())));
  EXPECT_THAT(t.err(), ::testing::HasSubstr("merging directory '/named/'"));
}

TEST(mkdwarfs_test, set_owner_takes_precedence_over_root_owner) {
  auto t = mkdwarfs_tester::create_empty();
  t.os->add_dir("/");
  t.os->add_file("/file", "data");

  ASSERT_EQ(
      0, t.run({"-o", "test.dwarfs", "-l1", "--set-owner=42", "--set-group=43",
                "--root-owner=7", "--root-group=8", "/file"}))
      << t.err();

  auto fs = t.fs_from_file("test.dwarfs");
  auto root = fs.find("/");

  ASSERT_TRUE(root);

  auto const st = fs.getattr(root->inode());

  EXPECT_EQ(42, st.uid());
  EXPECT_EQ(43, st.gid());
}

TEST(mkdwarfs_test, chmod_applies_on_top_of_root_mode) {
  auto t = mkdwarfs_tester::create_empty();
  t.os->add_dir("/");
  t.os->add_file("/file", "data");

  ASSERT_EQ(0, t.run({"-o", "test.dwarfs", "-l1", "--root-mode=0700",
                      "--chmod=go+rX", "/file"}))
      << t.err();

  auto fs = t.fs_from_file("test.dwarfs");
  auto root = fs.find("/");

  ASSERT_TRUE(root);
  EXPECT_EQ(0755, fs.getattr(root->inode()).mode() & 07777);
}

TEST(mkdwarfs_test, positional_root_attributes) {
  auto t = mkdwarfs_tester::create_empty();
  t.os->add_dir("/");
  t.os->add_dir("/src");
  t.os->add_file("/src/file", "data");

  ASSERT_EQ(0, t.run({"-o", "test.dwarfs", "/src/", "--root-mode=0700",
                      "--root-owner=1234", "--root-group=3456",
                      "--root-time=123456789", "--keep-all-times"}))
      << t.err();

  auto fs = t.fs_from_file("test.dwarfs");
  auto root = fs.find("/");
  ASSERT_TRUE(root);
  auto const st = fs.getattr(root->inode());
  EXPECT_EQ(0700, st.mode() & 07777);
  EXPECT_EQ(1234, st.uid());
  EXPECT_EQ(3456, st.gid());
  EXPECT_EQ(123456789, st.mtime());
  EXPECT_EQ(123456789, st.atime());
  EXPECT_EQ(123456789, st.ctime());
  EXPECT_TRUE(fs.find("/file"));
  EXPECT_FALSE(fs.find("/src"));
}

TEST(mkdwarfs_test, common_parent_produces_identical_image_for_named_children) {
  auto t = mkdwarfs_tester::create_empty();
  t.os->add_dir("/");

  // These attributes differ from the synthetic-root defaults.
  // Use fractional timestamps to ensure the full stat is preserved.
  simplestat st;
  st.ino = 100;
  st.mode = posix_file_type::directory | 0555;
  st.uid = 1003;
  st.gid = 103;
  st.atim = {1740552544, 123456789};
  st.mtim = {1740552544, 234567890};
  st.ctim = {1740552544, 345678901};
  t.os->add("/input", st);

  t.os->add_file("/input/alpha", "alpha contents");
  t.os->add_dir("/input/beta");
  t.os->add_file("/input/beta/nested", "nested contents");
  t.os->add_file("/input/zeta", "zeta contents");

  std::vector<std::string> const common{
      "-l1",
      "--no-create-timestamp",
      "--no-history",
      "--keep-all-times",
      "--time-resolution=us",
      "--order=path",
      "--num-walk-workers=1",
      "--num-scanner-workers=1",
      "--num-file-prep-workers=0",
      "--num-segmenter-workers=1",
  };

  auto args = common;
  args.insert(args.end(), {"-i", "/input", "-o", "old.dwarfs"});
  ASSERT_EQ(0, t.run(args)) << t.err();

  args = common;
  args.insert(args.end(), {"-o", "new.dwarfs", "--root-like=/input", "--",
                           "/input/alpha", "/input/beta", "/input/zeta"});
  ASSERT_EQ(0, t.run(args)) << t.err();

  EXPECT_EQ(t.get_file("old.dwarfs"), t.get_file("new.dwarfs"));

  args = common;
  args.insert(args.end(), {"-o", "inferred.dwarfs", "--", "/input/alpha",
                           "/input/beta", "/input/zeta"});
  ASSERT_EQ(0, t.run(args)) << t.err();
  EXPECT_EQ(t.get_file("old.dwarfs"), t.get_file("inferred.dwarfs"));
}

TEST(mkdwarfs_test, common_parent_inference_and_root_like_precedence) {
  auto t = mkdwarfs_tester::create_empty();
  t.os->add_dir("/");
  simplestat parent;
  parent.mode = posix_file_type::directory | 0710;
  parent.uid = 4242;
  parent.gid = 4243;
  parent.atim = {101, 123456789};
  parent.mtim = {102, 234567890};
  parent.ctim = {103, 345678901};
  t.os->add("/input", parent);
  t.os->add_file("/input/a", "a");
  t.os->add_file("/input/b", "b");

  simplestat override = parent;
  override.mode = posix_file_type::directory | 0750;
  override.uid = 5000;
  override.gid = 5001;
  override.mtim = {200, 987654321};
  t.os->add("/override", override);

  ASSERT_EQ(0, t.run({"-o", "inferred.dwarfs", "-l1", "--keep-all-times",
                      "--time-resolution=ns", "--", "/input/./a", "/input/b"}))
      << t.err();
  {
    auto image = t.fs_from_file("inferred.dwarfs");
    auto root = image.find("/");
    ASSERT_TRUE(root);
    auto const st = image.getattr(root->inode());
    EXPECT_EQ(0710, st.mode() & 07777);
    EXPECT_EQ(4242, st.uid());
    EXPECT_EQ(4243, st.gid());
    EXPECT_EQ((file_stat::timespec_type{101, 123456789}), st.atimespec());
    EXPECT_EQ((file_stat::timespec_type{102, 234567890}), st.mtimespec());
    EXPECT_EQ((file_stat::timespec_type{103, 345678901}), st.ctimespec());
  }

  ASSERT_EQ(0, t.run({"-o", "explicit.dwarfs", "-l1", "--keep-all-times",
                      "--time-resolution=ns", "--root-like=/override", "--",
                      "/input/a", "/input/b"}))
      << t.err();

  {
    auto image = t.fs_from_file("explicit.dwarfs");
    auto root = image.find("/");
    ASSERT_TRUE(root);
    auto const st = image.getattr(root->inode());
    EXPECT_EQ(0750, st.mode() & 07777);
    EXPECT_EQ(5000, st.uid());
    EXPECT_EQ(5001, st.gid());
    EXPECT_EQ((file_stat::timespec_type{200, 987654321}), st.mtimespec());
  }

  // Per-field root attributes override inferred metadata.
  ASSERT_EQ(0,
            t.run({"-o", "attributes.dwarfs", "-l1", "--root-mode=0700",
                   "--root-owner=6000", "--root-group=6001", "--root-time=300",
                   "--keep-all-times", "--", "/input/a", "/input/b"}))
      << t.err();

  {
    auto image = t.fs_from_file("attributes.dwarfs");
    auto root = image.find("/");
    ASSERT_TRUE(root);
    auto const st = image.getattr(root->inode());
    EXPECT_EQ(0700, st.mode() & 07777);
    EXPECT_EQ(6000, st.uid());
    EXPECT_EQ(6001, st.gid());
    EXPECT_EQ(300, st.atime());
    EXPECT_EQ(300, st.mtime());
    EXPECT_EQ(300, st.ctime());
  }
}

TEST(mkdwarfs_test, first_contents_source_defines_root_metadata) {
  auto t = mkdwarfs_tester::create_empty();
  t.os->add_dir("/");
  simplestat first;
  first.mode = posix_file_type::directory | 0701;
  first.uid = 1234;
  first.gid = 2345;
  first.atim = {10, 100000001};
  first.mtim = {20, 200000002};
  first.ctim = {30, 300000003};
  t.os->add("/first", first);
  t.os->add_file("/first/a", "a");
  t.os->add_dir("/second", {.mtim = file_stat::timespec_type{99, 0}});
  t.os->add_file("/second/b", "b");
  t.os->add_file("/third", "c", {.mtim = file_stat::timespec_type{100, 0}});

  ASSERT_EQ(
      0, t.run({"-o", "image.dwarfs", "-l1", "--keep-all-times",
                "--time-resolution=ns", "--", "/first/", "/second/", "/third"}))
      << t.err();

  auto image = t.fs_from_file("image.dwarfs");
  auto root = image.find("/");

  ASSERT_TRUE(root);

  auto const st = image.getattr(root->inode());
  EXPECT_EQ(0701, st.mode() & 07777);
  EXPECT_EQ(1234, st.uid());
  EXPECT_EQ(2345, st.gid());
  EXPECT_EQ((file_stat::timespec_type{10, 100000001}), st.atimespec());
  EXPECT_EQ((file_stat::timespec_type{20, 200000002}), st.mtimespec());
  EXPECT_EQ((file_stat::timespec_type{30, 300000003}), st.ctimespec());
}

TEST(mkdwarfs_test, root_like_with_explicit_overrides) {
  auto t = mkdwarfs_tester::create_empty();
  t.os->add_dir("/");
  t.os->add_dir("/input", {.mtim = file_stat::timespec_type{12345, 123456789}});
  t.os->add_file("/input/file", "contents");

  ASSERT_EQ(0,
            t.run({"-o", "test.dwarfs", "-l1", "--root-like=/input",
                   "--root-mode=0700", "--root-owner=2345", "--root-group=4567",
                   "--root-time=42", "--keep-all-times", "--", "/input/file"}))
      << t.err();

  auto image = t.fs_from_file("test.dwarfs");
  auto root = image.find("/");

  ASSERT_TRUE(root);

  auto const root_stat = image.getattr(root->inode());

  EXPECT_EQ(0700, root_stat.mode() & 07777);
  EXPECT_EQ(2345, root_stat.uid());
  EXPECT_EQ(4567, root_stat.gid());
  EXPECT_EQ(42, root_stat.atime());
  EXPECT_EQ(42, root_stat.mtime());
  EXPECT_EQ(42, root_stat.ctime());
  EXPECT_TRUE(image.find("/file"));
}

TEST(mkdwarfs_test, root_like_rejects_invalid_paths_and_recompression) {
  auto t = mkdwarfs_tester::create_empty();
  t.os->add_dir("/");
  t.os->add_dir("/input");
  t.os->add_file("/input/file", "contents");

  EXPECT_NE(0, t.run({"-o", "test.dwarfs", "--root-like=/missing", "--",
                      "/input/file"}));
  EXPECT_THAT(t.err(), ::testing::HasSubstr("cannot stat --root-like"));

  EXPECT_NE(0, t.run({"-o", "test.dwarfs", "--root-like=/input/file", "--",
                      "/input/file"}));
  EXPECT_THAT(t.err(), ::testing::HasSubstr("is not a directory"));

  EXPECT_NE(0, t.run({"-i", "old.dwarfs", "-o", "new.dwarfs", "--recompress",
                      "--root-like=/input"}));
  EXPECT_THAT(t.err(),
              ::testing::HasSubstr("root directory options cannot be used"));
}

TEST(mkdwarfs_test, synthetic_root_defaults) {
  auto t = mkdwarfs_tester::create_empty();
  t.os->add_dir("/");
  t.os->add_dir("/first", {.mtim = file_stat::timespec_type{100, 999999999}});
  t.os->add_file("/first/a", "a");
  t.os->add_dir("/second", {.mtim = file_stat::timespec_type{101, 123456789}});
  t.os->add_file("/second/b", "b");
  t.os->add_file("/third", "c",
                 {.mtim = file_stat::timespec_type{101, 987654321}});

  ASSERT_EQ(
      0, t.run({"-o", "test.dwarfs", "-l1", "--keep-all-times",
                "--time-resolution=ns", "--", "/third", "/first/", "/second/"}))
      << t.err();

  auto fs = t.fs_from_file("test.dwarfs");
  auto root = fs.find("/");
  ASSERT_TRUE(root);

  auto const st = fs.getattr(root->inode());

  EXPECT_EQ(0755, st.mode() & 07777);

#ifdef _WIN32
  EXPECT_EQ(0, st.uid());
  EXPECT_EQ(0, st.gid());
#else
  EXPECT_EQ(::geteuid(), st.uid());
  EXPECT_EQ(::getegid(), st.gid());
#endif

  auto const latest = file_stat::timespec_type{101, 987654321};
  EXPECT_EQ(latest, st.mtimespec());
  EXPECT_EQ(latest, st.atimespec());
  EXPECT_EQ(latest, st.ctimespec());

  // Explicit root time takes precedence over the timestamp fallback.
  ASSERT_EQ(0, t.run({"-o", "overridden.dwarfs", "-l1", "--keep-all-times",
                      "--root-time=42", "--", "/third", "/first/", "/second/"}))
      << t.err();
  auto overridden = t.fs_from_file("overridden.dwarfs");
  auto overridden_root = overridden.find("/");
  ASSERT_TRUE(overridden_root);
  auto const overridden_stat = overridden.getattr(overridden_root->inode());
  EXPECT_EQ(42, overridden_stat.mtime());
  EXPECT_EQ(42, overridden_stat.atime());
  EXPECT_EQ(42, overridden_stat.ctime());
}

TEST(mkdwarfs_test, reproducible_multi_source_without_root_options) {
  auto t = mkdwarfs_tester::create_empty();
  t.os->add_dir("/");
  t.os->add_dir("/one");
  t.os->add_file("/one/a", "a", {.mtim = file_stat::timespec_type{123, 1}});
  t.os->add_dir("/two", {.mtim = file_stat::timespec_type{456, 2}});
  t.os->add_file("/two/b", "b");

  std::vector<std::string> args{"-l1",
                                "--reproducible",
                                "--no-history",
                                "--order=path",
                                "--num-walk-workers=1",
                                "--num-scanner-workers=1",
                                "--num-file-prep-workers=0",
                                "--num-segmenter-workers=1"};
  auto run = [&](std::string const& output) {
    auto cmd = args;
    cmd.insert(cmd.end(), {"-o", output, "--", "/one/a", "/two/"});
    return t.run(cmd);
  };

  ASSERT_EQ(0, run("first.dwarfs")) << t.err();
  ASSERT_EQ(0, run("second.dwarfs")) << t.err();
  EXPECT_EQ(t.get_file("first.dwarfs"), t.get_file("second.dwarfs"));

  auto image = t.fs_from_file("first.dwarfs");
  auto root = image.find("/");
  ASSERT_TRUE(root);
  EXPECT_EQ(456, image.getattr(root->inode()).mtime());
}

TEST(mkdwarfs_test, root_time_now) {
  auto t = mkdwarfs_tester::create_empty();
  t.os->add_dir("/");
  t.os->add_file("/file", "data");

  auto const before = std::time(nullptr);
  ASSERT_EQ(0, t.run({"-o", "test.dwarfs", "-l1", "/file", "--root-time=now",
                      "--keep-all-times"}))
      << t.err();
  auto const after = std::time(nullptr);

  auto fs = t.fs_from_file("test.dwarfs");
  auto root = fs.find("/");
  ASSERT_TRUE(root);
  auto const st = fs.getattr(root->inode());
  EXPECT_GE(st.mtime(), before);
  EXPECT_LE(st.mtime(), after);
  EXPECT_EQ(st.mtime(), st.atime());
  EXPECT_EQ(st.mtime(), st.ctime());
}

TEST(mkdwarfs_test, root_time_epoch_string) {
  using namespace std::chrono_literals;
  using std::chrono::sys_days;

  auto t = mkdwarfs_tester::create_empty();
  t.os->add_dir("/");
  t.os->add_file("/file", "data");

  ASSERT_EQ(0, t.run({"-o", "test.dwarfs", "-l1", "/file", "--root-time",
                      "2020-01-01 01:02", "--keep-all-times"}))
      << t.err();

  auto fs = t.fs_from_file("test.dwarfs");
  auto root = fs.find("/");
  ASSERT_TRUE(root);
  auto const st = fs.getattr(root->inode());
  auto const expected =
      std::chrono::duration_cast<std::chrono::seconds>(
          (sys_days{2020y / 1 / 1} + 1h + 2min).time_since_epoch())
          .count();
  EXPECT_EQ(expected, st.mtime());
  EXPECT_EQ(expected, st.atime());
  EXPECT_EQ(expected, st.ctime());
}

TEST(mkdwarfs_test, synthetic_root_respects_set_time) {
  auto t = mkdwarfs_tester::create_empty();
  t.os->add_dir("/");
  t.os->add_file("/file", "data");

  ASSERT_EQ(0, t.run({"-o", "test.dwarfs", "-l1", "/file", "--set-time=42",
                      "--keep-all-times"}))
      << t.err();
  auto fs = t.fs_from_file("test.dwarfs");
  auto root = fs.find("/");
  auto file = fs.find("/file");
  ASSERT_TRUE(root);
  ASSERT_TRUE(file);
  for (auto inode : {root->inode(), file->inode()}) {
    auto const st = fs.getattr(inode);
    EXPECT_EQ(42, st.mtime());
    EXPECT_EQ(42, st.atime());
    EXPECT_EQ(42, st.ctime());
  }
}

TEST(mkdwarfs_test, synthetic_root_respects_source_date_epoch) {
  auto t = mkdwarfs_tester::create_empty();
  t.os->setenv("SOURCE_DATE_EPOCH", "12345");
  t.os->add_dir("/");
  t.os->add_dir("/one");
  t.os->add_file("/one/file", "data",
                 {.mtim = file_stat::timespec_type{20000, 0}});
  t.os->add_dir("/two");
  t.os->add_file("/two/other", "other");

  ASSERT_EQ(0, t.run({"-o", "test.dwarfs", "-l1", "--keep-all-times", "--",
                      "/one/file", "/two/other"}))
      << t.err();
  auto fs = t.fs_from_file("test.dwarfs");
  auto root = fs.find("/");
  ASSERT_TRUE(root);
  auto const st = fs.getattr(root->inode());
  EXPECT_EQ(12345, st.mtime());
  EXPECT_EQ(12345, st.atime());
  EXPECT_EQ(12345, st.ctime());
}

TEST(mkdwarfs_test, positional_source_errors) {
  auto t = mkdwarfs_tester::create_empty();
  EXPECT_NE(0, t.run({"-i", "/", "-o", "-", "/other"}));
  EXPECT_THAT(t.err(), ::testing::HasSubstr("positional sources"));
  EXPECT_NE(0, t.run({"--input-list", "-", "-o", "-", "/other"}));
  EXPECT_NE(0, t.run({"--recompress", "-o", "-", "/other"}));
  EXPECT_NE(0, t.run({"-o", "-", "/other/", "--root-mode=0888"}));
  EXPECT_NE(0, t.run({"-o", "-", "/other/", "--root-mode=10000"}));
  EXPECT_NE(0, t.run({"-o", "-", "/other/", "--root-time=InVaLiD"}));
  EXPECT_THAT(t.err(), ::testing::HasSubstr("cannot parse time point"));
  EXPECT_NE(0,
            t.run({"-o", "-", "/other/", "--root-time=18446744073709551615"}));
  EXPECT_THAT(t.err(), ::testing::HasSubstr("--root-time is out of range"));
}

TEST(mkdwarfs_test, missing_input_or_output_is_an_error) {
  {
    auto t = mkdwarfs_tester::create_empty();
    EXPECT_NE(0, t.run({"-o", "test.dwarfs"}));
    EXPECT_THAT(t.err(), ::testing::HasSubstr("no input specified"));
    EXPECT_TRUE(t.out().empty());
  }
  {
    auto t = mkdwarfs_tester::create_empty();
    EXPECT_NE(0, t.run({"/source/"}));
    EXPECT_THAT(t.err(), ::testing::HasSubstr("no output specified"));
    EXPECT_TRUE(t.out().empty());
  }
}

TEST(mkdwarfs_test, multitoken_option_cannot_consume_input_silently) {
  auto t = mkdwarfs_tester::create_empty();

  // -B consumes subsequent non-option arguments. Without `--`, these
  // paths are not positional sources and must trigger an error.
  EXPECT_NE(0, t.run({"-o", "test.dwarfs", "-B0", "/first/", "/second/"}));
  EXPECT_THAT(t.err(), ::testing::HasSubstr("no input specified"));
  EXPECT_TRUE(t.out().empty());
}

TEST(mkdwarfs_test, positional_sources_with_double_dash) {
  auto t = mkdwarfs_tester::create_empty();
  t.os->add_dir("/");
  t.os->add_dir("/source");
  t.os->add_file("/source/file", "contents");

  ASSERT_EQ(0, t.run({"-o", "test.dwarfs", "-l1", "--", "/source/"}))
      << t.err();
  auto fs = t.fs_from_file("test.dwarfs");
  EXPECT_TRUE(fs.find("/file"));
  EXPECT_FALSE(fs.find("/source"));
}

TEST(mkdwarfs_test, positional_single_named_file) {
  auto t = mkdwarfs_tester::create_empty();
  t.os->add_dir("/");
  t.os->add_file("/standalone", "data");

  ASSERT_EQ(0, t.run({"-o", "test.dwarfs", "-l1", "/standalone"})) << t.err();
  auto fs = t.fs_from_file("test.dwarfs");
  EXPECT_TRUE(fs.find("/standalone"));
  auto root = fs.find("/");
  ASSERT_TRUE(root);
  EXPECT_EQ(0755, fs.getattr(root->inode()).mode() & 0777);
}

TEST(mkdwarfs_test, root_attributes_with_legacy_input) {
  auto t = mkdwarfs_tester::create_empty();
  t.os->add_dir("/");
  t.os->add_dir("/src");
  t.os->add_file("/src/file", "data");

  ASSERT_EQ(0,
            t.run({"-i", "/src", "-o", "test.dwarfs", "-l1", "--root-mode=0711",
                   "--root-owner=123", "--root-group=456", "--root-time=42",
                   "--keep-all-times"}))
      << t.err();
  auto fs = t.fs_from_file("test.dwarfs");
  auto root = fs.find("/");
  ASSERT_TRUE(root);
  auto const st = fs.getattr(root->inode());
  EXPECT_EQ(0711, st.mode() & 07777);
  EXPECT_EQ(123, st.uid());
  EXPECT_EQ(456, st.gid());
  EXPECT_EQ(42, st.atime());
  EXPECT_EQ(42, st.mtime());
  EXPECT_EQ(42, st.ctime());
  EXPECT_TRUE(fs.find("/file"));
}

TEST_P(mkdwarfs_input_list_test, basic) {
  using namespace std::string_view_literals;
  static constexpr container::sorted_array_map input_lists{
      std::pair{path_type::absolute,
                "/somelink\n/foo.pl\n/somedir/ipsum.py\n"sv},
      std::pair{path_type::relative, "somelink\nfoo.pl\nsomedir/ipsum.py\n"sv},
      std::pair{path_type::mixed, "somelink\n/foo.pl\nsomedir/ipsum.py\n"sv},
  };

  auto [mode, type] = GetParam();
  std::string const image_file = "test.dwarfs";
  std::string const input_list{input_lists.at(type)};
  mkdwarfs_tester t;
  auto const input_file = t.set_input_list(mode, input_list);

  std::vector<std::string> args = {"--input-list", input_file, "-o", image_file,
                                   t.safe_log_level_opt(logger::TRACE)};
  if (type != path_type::relative) {
    args.push_back("-i");
    args.push_back("/");
  }

  ASSERT_EQ(0, t.run(args)) << t.err();

  std::ostringstream oss;
  t.add_stream_logger(oss, std::min(logger::DEBUG, kMaxSupportedLogLevel));

  auto fs = t.fs_from_file(image_file);

  auto link = fs.find("/somelink");
  auto foo = fs.find("/foo.pl");
  auto ipsum = fs.find("/somedir/ipsum.py");

  ASSERT_TRUE(link);
  ASSERT_TRUE(foo);
  ASSERT_TRUE(ipsum);

  EXPECT_FALSE(fs.find("/test.pl"));

  EXPECT_TRUE(link->inode().is_symlink());
  EXPECT_TRUE(foo->inode().is_regular_file());
  EXPECT_TRUE(ipsum->inode().is_regular_file());

  std::set<fs::path> const expected = {"", "somelink", "foo.pl", "somedir",
                                       fs::path("somedir") / "ipsum.py"};
  auto const actual = get_all_fs_paths(fs);

  EXPECT_EQ(expected, actual);
}

TEST_P(mkdwarfs_input_list_test, with_abs_input_dir) {
  using namespace std::string_view_literals;
  static constexpr container::sorted_array_map input_lists{
      std::pair{path_type::absolute,
                "/somedir/ipsum.py\n/somedir/empty\n/foo/bar\n"sv},
      std::pair{path_type::relative, "ipsum.py\nempty\n"sv},
      std::pair{path_type::mixed, "/somedir/ipsum.py\nempty\n"sv},
  };

  auto [mode, type] = GetParam();
  std::string const image_file = "test.dwarfs";
  std::string const input_list{input_lists.at(type)};
  mkdwarfs_tester t;
  auto const input_file = t.set_input_list(mode, input_list);

  // The out-of-root entry in the absolute input list triggers an error
  ASSERT_EQ(type == path_type::absolute ? 2 : 0,
            t.run({"--input-list", input_file, "-i", "/somedir", "-o",
                   image_file, t.safe_log_level_opt(logger::TRACE)}))
      << t.err();

  if (type == path_type::absolute) {
    EXPECT_THAT(t.err(), ::testing::HasSubstr(
                             "path '/foo/bar' is not below '/somedir'"));
  } else {
    EXPECT_THAT(t.err(), ::testing::Not(::testing::HasSubstr("not below")));
  }

  std::ostringstream oss;
  t.add_stream_logger(oss, std::min(logger::DEBUG, kMaxSupportedLogLevel));

  auto fs = t.fs_from_file(image_file);

  auto ipsum = fs.find("/ipsum.py");
  auto empty = fs.find("/empty");

  ASSERT_TRUE(ipsum);
  ASSERT_TRUE(empty);

  EXPECT_FALSE(fs.find("/test.pl"));

  EXPECT_TRUE(ipsum->inode().is_regular_file());
  EXPECT_TRUE(empty->inode().is_regular_file());

  std::set<fs::path> const expected = {"", "ipsum.py", "empty"};
  auto const actual = get_all_fs_paths(fs);

  EXPECT_EQ(expected, actual);
}

TEST_P(mkdwarfs_input_list_test, trailing_dir_slash_gh370) {
  using namespace std::string_view_literals;
  static constexpr container::sorted_array_map input_lists{
      std::pair{path_type::absolute,
                "/somedir/\n/somedir/ipsum.py\n/somedir/empty\n"sv},
      std::pair{path_type::relative,
                "somedir/\nsomedir/ipsum.py\nsomedir/empty\n"sv},
      std::pair{path_type::mixed,
                "/somedir/\nsomedir/ipsum.py\n/somedir/empty\n"sv},
  };

  auto [mode, type] = GetParam();
  std::string const image_file = "test.dwarfs";
  std::string const input_list{input_lists.at(type)};
  mkdwarfs_tester t;
  auto const input_file = t.set_input_list(mode, input_list);

  ASSERT_EQ(0, t.run({"--input-list", input_file, "-i", "/", "-o", image_file,
                      "-P", "plain"}))
      << t.err();

  auto fs = t.fs_from_file(image_file);

  auto ipsum = fs.find("/somedir/ipsum.py");
  auto empty = fs.find("/somedir/empty");

  ASSERT_TRUE(ipsum);
  ASSERT_TRUE(empty);

  EXPECT_FALSE(fs.find("/test.pl"));

  EXPECT_TRUE(ipsum->inode().is_regular_file());
  EXPECT_TRUE(empty->inode().is_regular_file());

  std::set<fs::path> const expected = {
      "",
      "somedir",
      "somedir/ipsum.py",
      "somedir/empty",
  };
  auto const actual = get_all_fs_paths(fs);

  EXPECT_EQ(expected, actual);

  auto meta = fs.info_as_json(
      {.features = {reader::fsinfo_feature::metadata_full_dump}});

  EXPECT_THAT(meta["full_metadata"]["names"],
              ::testing::Not(::testing::Contains("")));
}

INSTANTIATE_TEST_SUITE_P(dwarfs, mkdwarfs_input_list_test,
                         ::testing::Combine(::testing::ValuesIn(input_modes),
                                            ::testing::ValuesIn(path_types)));

TEST(mkdwarfs_test, input_list_with_rel_input_dir) {
  std::string const image_file = "test.dwarfs";
  std::string const input_list = "ipsum.py\nipsum.py\nempty\n";
  mkdwarfs_tester t;

  t.iol->set_in(input_list);

  ASSERT_EQ(2, t.run({"--input-list", "-", "-i", "somedir", "-o", image_file,
                      t.safe_log_level_opt(logger::TRACE)}))
      << t.err();

  EXPECT_THAT(t.err(),
              ::testing::HasSubstr(
                  "skipping duplicate entry 'ipsum.py' in input list"));

  std::ostringstream oss;
  t.add_stream_logger(oss, std::min(logger::DEBUG, kMaxSupportedLogLevel));

  auto fs = t.fs_from_file(image_file);

  auto ipsum = fs.find("/ipsum.py");
  auto empty = fs.find("/empty");

  ASSERT_TRUE(ipsum);
  ASSERT_TRUE(empty);

  EXPECT_FALSE(fs.find("/test.pl"));

  EXPECT_TRUE(ipsum->inode().is_regular_file());
  EXPECT_TRUE(empty->inode().is_regular_file());

  std::set<fs::path> const expected = {"", "ipsum.py", "empty"};
  auto const actual = get_all_fs_paths(fs);

  EXPECT_EQ(expected, actual);
}

TEST(mkdwarfs_test, input_list_abs_list_path_requires_abs_root_dir) {
  std::string const image_file = "test.dwarfs";
  std::string const input_list = "/ipsum.py\n";
  mkdwarfs_tester t;

  t.iol->set_in(input_list);

  EXPECT_NE(0, t.run({"--input-list", "-", "-i", "somedir", "-o", image_file,
                      t.safe_log_level_opt(logger::TRACE)}))
      << t.err();

  EXPECT_THAT(t.err(), ::testing::HasSubstr(
                           "absolute paths in input list require absolute "
                           "input path, but input path is 'somedir'"));
}

TEST(mkdwarfs_test, input_list_large) {
  auto t = mkdwarfs_tester::create_empty();
  t.add_root_dir();
  auto paths = t.add_random_file_tree({.avg_size = 32.0, .dimension = 32});

  {
    std::ostringstream os;
    for (auto const& p : paths) {
      os << p.first.string() << '\n';
    }
    t.iol->set_in(os.str());
  }

  ASSERT_EQ(0, t.run({"-l3", "--input-list", "-", "-o", "-"})) << t.err();

  auto fs = t.fs_from_stdout();

  std::set<fs::path> expected;
  std::ranges::transform(paths, std::inserter(expected, expected.end()),
                         [](auto const& p) { return p.first; });
  std::set<fs::path> actual;

  fs.walk([&](auto const& e) {
    if (e.inode().is_regular_file()) {
      actual.insert(e.fs_path());
    }
  });

  EXPECT_EQ(expected, actual);
}

TEST(mkdwarfs_test, input_list_preserve_order) {
  auto t = mkdwarfs_tester::create_empty();
  t.add_root_dir();
  // make sure files are large enough that there are no duplicates
  auto paths = t.add_random_file_tree({
      .avg_size = 32.0,
      .min_size = 16,
      .dimension = 20,
      .only_random_contents = true,
  });

  {
    std::ostringstream os;
    for (auto const& p : paths) {
      os << p.first.string() << '\n';
    }
    t.iol->set_in(os.str());
  }

  // Disable segmentation (-B0) to ensure *every* file has its data
  // written directly to the output. However, we rely on "background"
  // file hashing (for deduplication) to process files in parallel
  // and add some randomness to inode order before it is being restored
  // to input order by --order=none later.
  ASSERT_EQ(0, t.run({"-l3", "--input-list", "-", "-o", "-", "--order=none",
                      "-B0", "-S16"}))
      << t.err();

  auto fs = t.fs_from_stdout();

  std::vector<fs::path> expected;
  for (auto const& p : paths) {
    expected.push_back(p.first);
  }

  std::vector<std::pair<fs::path, size_t>> actual_with_offset;

  fs.walk([&](auto const& e) {
    if (e.inode().is_regular_file()) {
      auto const info = fs.get_inode_info(e.inode());
      auto const first_chunk = info["chunks"][0];
      auto const offset = (first_chunk["block"].template get<size_t>() << 16) +
                          first_chunk["offset"].template get<size_t>();
      actual_with_offset.emplace_back(e.fs_path(), offset);
    }
  });

  std::ranges::sort(actual_with_offset, [](auto const& a, auto const& b) {
    return a.second < b.second;
  });

  std::vector<fs::path> actual;
  for (auto const& [path, _] : actual_with_offset) {
    actual.push_back(path);
  }

  EXPECT_THAT(actual, ::testing::ElementsAreArray(expected));
}

TEST(mkdwarfs_test, input_list_with_leading_dots_github292) {
  std::string const image_file = "test.dwarfs";
  std::string const input_list = "./ipsum.py\n././ipsum.py\n./empty\n";
  mkdwarfs_tester t;

  t.iol->set_in(input_list);

  ASSERT_EQ(2, t.run({"--input-list", "-", "-i", "somedir", "-o", image_file,
                      t.safe_log_level_opt(logger::TRACE)}))
      << t.err();

  EXPECT_THAT(t.err(),
              ::testing::HasSubstr(
                  "skipping duplicate entry 'ipsum.py' in input list"));

  std::ostringstream oss;
  t.add_stream_logger(oss, std::min(logger::DEBUG, kMaxSupportedLogLevel));

  auto fs = t.fs_from_file(image_file);

  auto ipsum = fs.find("/ipsum.py");
  auto empty = fs.find("/empty");

  ASSERT_TRUE(ipsum);
  ASSERT_TRUE(empty);

  EXPECT_FALSE(fs.find("/test.pl"));

  EXPECT_TRUE(ipsum->inode().is_regular_file());
  EXPECT_TRUE(empty->inode().is_regular_file());

  std::set<fs::path> const expected = {"", "ipsum.py", "empty"};
  auto const actual = get_all_fs_paths(fs);

  EXPECT_EQ(expected, actual);
}

TEST(mkdwarfs_test, metadata_inode_info) {
  auto t = mkdwarfs_tester::create_empty();
  t.add_root_dir();
  t.os->add_local_files(audio_data_dir);
  t.os->add_local_files(fits_data_dir);
  t.os->add_file("random", 4096, true);

  ASSERT_EQ(0, t.run("-l3 -i / -o - --categorize -S10")) << t.err();

  auto fs = t.fs_from_stdout();

  {
    auto dev = fs.find("/test8.aiff");
    ASSERT_TRUE(dev);

    auto info = fs.get_inode_info(dev->inode());
    ASSERT_TRUE(info.count("chunks") > 0);

    std::set<std::string> categories;

    EXPECT_GE(info["chunks"].size(), 2);

    for (auto chunk : info["chunks"]) {
      ASSERT_TRUE(chunk.count("category") > 0);
      categories.insert(chunk["category"].get<std::string>());
    }

    std::set<std::string> expected{
        "pcmaudio/metadata",
        "pcmaudio/waveform",
    };

    EXPECT_EQ(expected, categories);
  }

  {
    auto dev = fs.find("/test.fits");
    ASSERT_TRUE(dev);

    auto info = fs.get_inode_info(dev->inode());
    ASSERT_TRUE(info.count("chunks") > 0);

    std::set<std::string> categories;

    auto chunk_count = info["chunks"].size();

    EXPECT_GE(chunk_count, 12);

    for (auto chunk : info["chunks"]) {
      ASSERT_TRUE(chunk.count("category") > 0);
      categories.insert(chunk["category"].get<std::string>());
    }

    std::set<std::string> expected{
        "fits/image",
        "fits/metadata",
    };

    EXPECT_EQ(expected, categories);

    info = fs.get_inode_info(dev->inode(), 5);
    ASSERT_TRUE(info.count("chunks") > 0);

    EXPECT_EQ(fmt::format("too many chunks ({})", chunk_count),
              info["chunks"].get<std::string>());
  }
}

TEST(mkdwarfs_test, metadata_path) {
  fs::path const f1{"test.txt"};
  fs::path const f2{U"猫.txt"};
  fs::path const f3{u8"⚽️.bin"};
  fs::path const f4{L"Карибського"};
  fs::path const d1{u8"我爱你"};
  fs::path const f5{d1 / u8"☀️ Sun"};

  auto t = mkdwarfs_tester::create_empty();
  t.add_root_dir();
  t.os->add_file(f1, 2, true);
  t.os->add_file(f2, 4, true);
  t.os->add_file(f3, 8, true);
  t.os->add_file(f4, 16, true);
  t.os->add_dir(d1);
  t.os->add_file(f5, 32, true);
  ASSERT_EQ(0, t.run("-l3 -i / -o -"));
  auto fs = t.fs_from_stdout();

  std::map<size_t, reader::dir_entry_view> entries;
  fs.walk([&](auto e) {
    auto stat = fs.getattr(e.inode());
    if (stat.is_regular_file()) {
      entries.emplace(stat.size(), e);
    }
  });

  ASSERT_EQ(entries.size(), 5);

  auto e1 = entries.at(2);
  auto e2 = entries.at(4);
  auto e3 = entries.at(8);
  auto e4 = entries.at(16);
  auto e5 = entries.at(32);

  auto dev = fs.find(d1.string());
  ASSERT_TRUE(dev);
  auto iv = dev->inode();

  EXPECT_EQ(iv.mode_string(), "---drwxr-xr-x");
  EXPECT_EQ(e1.inode().mode_string(), "----rw-r--r--");

  EXPECT_EQ(e1.fs_path(), f1);
  EXPECT_EQ(e2.fs_path(), f2);
  EXPECT_EQ(e3.fs_path(), f3);
  EXPECT_EQ(e4.fs_path(), f4);
  EXPECT_EQ(e5.fs_path(), f5);

  EXPECT_EQ(e1.wpath(), L"test.txt");
  EXPECT_EQ(e2.wpath(), L"猫.txt");
  EXPECT_EQ(e3.wpath(), L"⚽️.bin");
  EXPECT_EQ(e4.wpath(), L"Карибського");
#ifdef _WIN32
  EXPECT_EQ(e5.wpath(), L"我爱你\\☀️ Sun");
#else
  EXPECT_EQ(e5.wpath(), L"我爱你/☀️ Sun");
#endif

  EXPECT_EQ(e1.path(), "test.txt");
  EXPECT_EQ(e2.path(), "猫.txt");
  EXPECT_EQ(e3.path(), "⚽️.bin");
  EXPECT_EQ(e4.path(), "Карибського");
#ifdef _WIN32
  EXPECT_EQ(e5.path(), "我爱你\\☀️ Sun");
#else
  EXPECT_EQ(e5.path(), "我爱你/☀️ Sun");
#endif

  EXPECT_EQ(e1.unix_path(), "test.txt");
  EXPECT_EQ(e2.unix_path(), "猫.txt");
  EXPECT_EQ(e3.unix_path(), "⚽️.bin");
  EXPECT_EQ(e4.unix_path(), "Карибського");
  EXPECT_EQ(e5.unix_path(), "我爱你/☀️ Sun");
}

TEST(mkdwarfs_test, metadata_modes) {
  mkdwarfs_tester t;
  t.add_special_files();
  ASSERT_EQ(0, t.run("-l3 -i / -o - --with-specials --with-devices"));
  auto fs = t.fs_from_stdout();

  auto d1 = fs.find("/");
  auto d2 = fs.find("/foo.pl");
  auto d3 = fs.find("/somelink");
  auto d4 = fs.find("/somedir");
  auto d5 = fs.find("/somedir/pipe");
  auto d6 = fs.find("/somedir/null");
  auto d7 = fs.find("/suid");
  auto d8 = fs.find("/sgid");
  auto d9 = fs.find("/sticky");
  auto d10 = fs.find("/block");
  auto d11 = fs.find("/sock");

  ASSERT_TRUE(d1);
  ASSERT_TRUE(d2);
  ASSERT_TRUE(d3);
  ASSERT_TRUE(d4);
  ASSERT_TRUE(d5);
  ASSERT_TRUE(d6);
  ASSERT_TRUE(d7);
  ASSERT_TRUE(d8);
  ASSERT_TRUE(d9);
  ASSERT_TRUE(d10);
  ASSERT_TRUE(d11);

  EXPECT_EQ(d1->inode().mode_string(), "---drwxrwxrwx");
  EXPECT_EQ(d2->inode().mode_string(), "----rw-------");
  EXPECT_EQ(d3->inode().mode_string(), "---lrwxrwxrwx");
  EXPECT_EQ(d4->inode().mode_string(), "---drwxrwxrwx");
  EXPECT_EQ(d5->inode().mode_string(), "---prw-r--r--");
  EXPECT_EQ(d6->inode().mode_string(), "---crw-rw-rw-");
  EXPECT_EQ(d7->inode().mode_string(), "U---rwxr-xr-x");
  EXPECT_EQ(d8->inode().mode_string(), "-G--rwxr-xr-x");
  EXPECT_EQ(d9->inode().mode_string(), "--S-rwxr-xr-x");
  EXPECT_EQ(d10->inode().mode_string(), "---brw-rw-rw-");
  EXPECT_EQ(d11->inode().mode_string(), "---srw-rw-rw-");
}

TEST(mkdwarfs_test, metadata_specials) {
  mkdwarfs_tester t;
  t.add_special_files();
  ASSERT_EQ(0, t.run("-l3 -i / -o - --with-specials --with-devices"));
  auto fs = t.fs_from_stdout();

  std::ostringstream oss;
  fs.dump(oss, {.features = reader::fsinfo_features::all()});
  auto dump = oss.str();

  auto meta = fs.metadata_as_json();
  std::set<std::string> types;
  for (auto const& ino : meta["root"]["inodes"]) {
    types.insert(ino["type"].get<std::string>());
    if (auto di = ino.find("inodes"); di != ino.end()) {
      for (auto const& ino2 : *di) {
        types.insert(ino2["type"].get<std::string>());
      }
    }
  }
  std::set<std::string> expected_types = {
      "file", "link", "directory", "chardev", "blockdev", "socket", "fifo"};
  EXPECT_EQ(expected_types, types);

  EXPECT_THAT(dump, ::testing::HasSubstr("char device"));
  EXPECT_THAT(dump, ::testing::HasSubstr("block device"));
  EXPECT_THAT(dump, ::testing::HasSubstr("socket"));
  EXPECT_THAT(dump, ::testing::HasSubstr("named pipe"));

  auto dev = fs.find("/block");
  ASSERT_TRUE(dev);

  std::error_code ec;
  auto stat = fs.getattr(dev->inode(), ec);
  EXPECT_FALSE(ec);

  EXPECT_TRUE(stat.is_device());
  EXPECT_EQ(77, stat.rdev());
}

TEST(mkdwarfs_test, metadata_time_resolution) {
  mkdwarfs_tester t;
  t.add_special_files();
  ASSERT_EQ(0, t.run("-l3 -i / -o - --time-resolution=min --keep-all-times"));
  auto fs = t.fs_from_stdout();

  std::ostringstream oss;
  fs.dump(oss, {.features = reader::fsinfo_features::all()});
  auto dump = oss.str();

  EXPECT_THAT(dump, ::testing::HasSubstr("time resolution: 60 seconds"));

  auto dyn = fs.info_as_json({.features = reader::fsinfo_features::all()});
  EXPECT_EQ(60, dyn["time_resolution"].get<int>());

  auto dev = fs.find("/suid");
  ASSERT_TRUE(dev);

  std::error_code ec;
  auto stat = fs.getattr(dev->inode(), ec);
  EXPECT_FALSE(ec);
  EXPECT_EQ(3300, stat.atime());
  EXPECT_EQ(2220, stat.mtime());
  EXPECT_EQ(1080, stat.ctime());
}

TEST(mkdwarfs_test, metadata_readdir) {
  mkdwarfs_tester t;
  ASSERT_EQ(0, t.run("-l3 -i / -o -"));
  auto fs = t.fs_from_stdout();

  auto dev = fs.find("/somedir");
  ASSERT_TRUE(dev);
  auto iv = dev->inode();

  auto dir = fs.opendir(iv);
  ASSERT_TRUE(dir);

  {
    auto r = fs.readdir(dir.value(), 0);
    ASSERT_TRUE(r);

    EXPECT_EQ(".", r->name());
    EXPECT_EQ(r->inode().inode_num(), iv.inode_num());
  }

  {
    auto r = fs.readdir(dir.value(), 1);
    ASSERT_TRUE(r);

    EXPECT_EQ("..", r->name());

    auto parent = fs.find("/");
    ASSERT_TRUE(parent);
    EXPECT_EQ(r->inode().inode_num(), parent->inode().inode_num());
  }

  {
    auto r = fs.readdir(dir.value(), 100);
    EXPECT_FALSE(r);
  }
}

TEST(mkdwarfs_test, metadata_directory_iterator) {
  mkdwarfs_tester t;
  t.os->add_dir("emptydir");
  ASSERT_EQ(0, t.run("-l3 -i / -o -"));
  auto fs = t.fs_from_stdout();

  std::map<std::string, std::vector<std::string>> testdirs{
      {"",
       {"bar.pl", "baz.pl", "empty", "emptydir", "foo.pl", "ipsum.txt",
        "somedir", "somelink", "test.pl"}},
      {"somedir", {"bad", "empty", "ipsum.py"}},
      {"emptydir", {}},
  };

  for (auto const& [path, expected_names] : testdirs) {
    auto dev = fs.find(path);
    ASSERT_TRUE(dev) << path;

    auto dir = fs.opendir(dev->inode());
    ASSERT_TRUE(dir) << path;

    std::vector<std::string> actual_names;
    std::vector<std::string> actual_paths;
    for (auto const& dev : *dir) {
      actual_names.push_back(dev.name());
      actual_paths.push_back(dev.unix_path());
    }

    std::vector<std::string> expected_paths;
    for (auto const& name : expected_names) {
      expected_paths.push_back(path.empty() ? name : path + "/" + name);
    }

    EXPECT_EQ(expected_names, actual_names) << path;
    EXPECT_EQ(expected_paths, actual_paths) << path;
  }
}

TEST(mkdwarfs_test, metadata_access) {
#ifdef _WIN32
#define F_OK 0
#define W_OK 2
#define R_OK 4
  static constexpr int const x_ok = 1;
#else
  static constexpr int const x_ok = X_OK;
#endif

  auto t = mkdwarfs_tester::create_empty();
  t.add_root_dir();
  t.os->add("access", {1001, 040742, 1, 222, 3333});
  ASSERT_EQ(0, t.run("-l3 -i / -o -"));

  {
    auto fs = t.fs_from_stdout();

    auto dev = fs.find("/access");
    ASSERT_TRUE(dev);
    auto iv = dev->inode();

    EXPECT_TRUE(fs.access(iv, F_OK, 1, 1));

    EXPECT_FALSE(fs.access(iv, R_OK, 1, 1));
    EXPECT_TRUE(fs.access(iv, W_OK, 1, 1));
    EXPECT_FALSE(fs.access(iv, x_ok, 1, 1));

    EXPECT_TRUE(fs.access(iv, R_OK, 1, 3333));
    EXPECT_TRUE(fs.access(iv, W_OK, 1, 3333));
    EXPECT_FALSE(fs.access(iv, x_ok, 1, 3333));

    EXPECT_TRUE(fs.access(iv, R_OK, 222, 7));
    EXPECT_TRUE(fs.access(iv, W_OK, 222, 7));
    EXPECT_TRUE(fs.access(iv, x_ok, 222, 7));
  }

  {
    auto fs = t.fs_from_stdout({.metadata = {.readonly = true}});

    auto dev = fs.find("/access");
    ASSERT_TRUE(dev);
    auto iv = dev->inode();

    EXPECT_TRUE(fs.access(iv, F_OK, 1, 1));

    EXPECT_FALSE(fs.access(iv, R_OK, 1, 1));
    EXPECT_FALSE(fs.access(iv, W_OK, 1, 1));
    EXPECT_FALSE(fs.access(iv, x_ok, 1, 1));

    EXPECT_TRUE(fs.access(iv, R_OK, 1, 3333));
    EXPECT_FALSE(fs.access(iv, W_OK, 1, 3333));
    EXPECT_FALSE(fs.access(iv, x_ok, 1, 3333));

    EXPECT_TRUE(fs.access(iv, R_OK, 222, 7));
    EXPECT_FALSE(fs.access(iv, W_OK, 222, 7));
    EXPECT_TRUE(fs.access(iv, x_ok, 222, 7));
  }
}

TEST(mkdwarfs_test, chmod_norm) {
  std::string const image_file = "test.dwarfs";

  std::set<std::string> real, norm;

  {
    mkdwarfs_tester t;
    ASSERT_EQ(0, t.run({"-i", "/", "-o", image_file}));
    auto fs = t.fs_from_file(image_file);
    fs.walk([&](auto const& e) { real.insert(e.inode().perm_string()); });
  }

  {
    mkdwarfs_tester t;
    ASSERT_EQ(0, t.run({"-i", "/", "-o", image_file, "--chmod=norm"}));
    auto fs = t.fs_from_file(image_file);
    fs.walk([&](auto const& e) { norm.insert(e.inode().perm_string()); });
  }

  EXPECT_NE(real, norm);

  std::set<std::string> expected_norm = {"r--r--r--", "r-xr-xr-x"};

  EXPECT_EQ(expected_norm, norm);
}

TEST(mkdwarfs_test, dump_inodes) {
  std::string const image_file = "test.dwarfs";
  std::string const inode_file = "inode.dump";

  auto t = mkdwarfs_tester::create_empty();
  t.add_root_dir();
  t.os->add_local_files(audio_data_dir);
  t.os->add_local_files(fits_data_dir);
  t.os->add_file("random", 4096, true);
  t.os->add_file("large", 32 * 1024 * 1024);
  t.add_random_file_tree({.avg_size = 1024.0, .dimension = 8});
  t.os->setenv("DWARFS_DUMP_INODES", inode_file);

  ASSERT_EQ(0, t.run({"-i", "/", "-o", image_file, "--categorize", "-W8"}));

  auto dump = t.fa->get_file(inode_file);

  ASSERT_TRUE(dump);
  EXPECT_GT(dump->size(), 1000) << dump.value();
}

TEST(mkdwarfs_test, set_time_now) {
  auto t0 =
      std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

  auto [regfs, regt] = build_with_args();
  ASSERT_TRUE(regfs) << regt.err();
  auto reg = get_all_fs_times(*regfs);

  auto [optfs, optt] = build_with_args({"--set-time=now"});
  ASSERT_TRUE(optfs) << optt.err();
  auto opt = get_all_fs_times(*optfs);

  auto t1 =
      std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

  ASSERT_EQ(reg.size(), 11);
  ASSERT_EQ(opt.size(), 1);

  EXPECT_GE(*opt.begin(), t0);
  EXPECT_LE(*opt.begin(), t1);
}

TEST(mkdwarfs_test, set_time_epoch) {
  auto [regfs, regt] = build_with_args();
  ASSERT_TRUE(regfs) << regt.err();
  auto reg = get_all_fs_times(*regfs);

  auto [optfs, optt] = build_with_args({"--set-time=100000001"});
  ASSERT_TRUE(optfs) << optt.err();
  auto opt = get_all_fs_times(*optfs);

  EXPECT_EQ(reg.size(), 11);
  ASSERT_EQ(opt.size(), 1);

  EXPECT_EQ(*opt.begin(), 100000001);
}

TEST(mkdwarfs_test, set_time_epoch_string) {
  using namespace std::chrono_literals;
  using std::chrono::sys_days;

  auto [optfs, optt] = build_with_args({"--set-time", "2020-01-01 01:02"});
  ASSERT_TRUE(optfs) << optt.err();
  auto opt = get_all_fs_times(*optfs);

  ASSERT_EQ(opt.size(), 1);

  EXPECT_EQ(*opt.begin(),
            std::chrono::duration_cast<std::chrono::seconds>(
                (sys_days{2020y / 1 / 1} + 1h + 2min).time_since_epoch())
                .count());
}

TEST(mkdwarfs_test, set_owner) {
  auto [regfs, regt] = build_with_args();
  ASSERT_TRUE(regfs) << regt.err();
  auto reg = get_all_fs_uids(*regfs);

  auto [optfs, optt] = build_with_args({"--set-owner=333"});
  ASSERT_TRUE(optfs) << optt.err();
  auto opt = get_all_fs_uids(*optfs);

  ASSERT_EQ(reg.size(), 2);
  ASSERT_EQ(opt.size(), 1);

  EXPECT_EQ(*opt.begin(), 333);
}

TEST(mkdwarfs_test, set_group) {
  auto [regfs, regt] = build_with_args();
  ASSERT_TRUE(regfs) << regt.err();
  auto reg = get_all_fs_gids(*regfs);

  auto [optfs, optt] = build_with_args({"--set-group=444"});
  ASSERT_TRUE(optfs) << optt.err();
  auto opt = get_all_fs_gids(*optfs);

  ASSERT_EQ(reg.size(), 2);
  ASSERT_EQ(opt.size(), 1);

  EXPECT_EQ(*opt.begin(), 444);
}

namespace {

struct cli_error_case {
  std::string name;
  std::vector<std::string> args;
  std::string expected_error;
};

std::ostream& operator<<(std::ostream& os, cli_error_case const& c) {
  return os << c.name;
}

struct cli_error_name {
  template <typename ParamType>
  std::string operator()(testing::TestParamInfo<ParamType> const& info) const {
    return info.param.name;
  }
};

class mkdwarfs_cli_error_test : public testing::TestWithParam<cli_error_case> {
};

std::vector<cli_error_case> const option_errors{
    {"unknown_option", {"--grmpf"}, "unrecognised option"},
    {"invalid_compression_level",
     {"-i", "/", "-o", "-", "-l", "10"},
     "invalid compression level"},
    {"block_size_too_small",
     {"-i", "/", "-o", "-", "-S", "1"},
     "block size must be between"},
    {"block_size_too_large",
     {"-i", "/", "-o", "-", "-S", "100"},
     "block size must be between"},
    {"set_time_error",
     {"-i", "/", "-o", "-", "--set-time=InVaLiD"},
     "cannot parse time point"},
    {"time_resolution_zero",
     {"-i", "/", "-o", "-", "--time-resolution=0"},
     "'--time-resolution' must be nonzero"},
    {"time_resolution_invalid",
     {"-i", "/", "-o", "-", "--time-resolution=grmpf"},
     "'--time-resolution' is invalid"},
    {"unknown_categorizer",
     {"-i", "/", "-o", "-", "--categorize=grmpf"},
     "unknown categorizer: grmpf"},
};

std::vector<cli_error_case> const order_errors{
    {"order_invalid",
     {"-i", "/", "-o", "-", "--order=grmpf"},
     "invalid inode order mode"},
    {"order_does_not_support_options",
     {"-i", "/", "-o", "-", "--order=path:foo=42"},
     "inode order mode 'path' does not support options"},
    {"order_nilsimsa_invalid_option",
     {"-i", "/", "-o", "-", "--order=nilsimsa:grmpf"},
     "invalid option(s) for choice 'nilsimsa': grmpf"},
    {"order_nilsimsa_invalid_max_children_value",
     {"-i", "/", "-o", "-", "--order=nilsimsa:max-children=0"},
     "invalid max-children value: 0"},
    {"order_nilsimsa_invalid_max_cluster_size_value_zero",
     {"-i", "/", "-o", "-", "--order=nilsimsa:max-cluster-size=0"},
     "invalid max-cluster-size value: 0"},
    {"order_nilsimsa_invalid_max_cluster_size_value_negative",
     {"-i", "/", "-o", "-", "--order=nilsimsa:max-cluster-size=-1"},
     "invalid max-cluster-size value: -1"},
    {"order_nilsimsa_duplicate_option",
     {"-i", "/", "-o", "-",
      "--order=nilsimsa:max-cluster-size=1:max-cluster-size=10"},
     "duplicate option 'max-cluster-size' for choice 'nilsimsa'"},
};

std::vector<cli_error_case> const chmod_errors{
    {"missing_whom",
     {"-i", "/", "-o", "-", "--chmod=invalid"},
     "invalid metadata option: missing whom in chmod mode: invalid"},
    {"empty_mode_trailing_comma",
     {"-i", "/", "-o", "-", "--chmod=a+r,"},
     "invalid metadata option: empty chmod mode"},
    {"empty_mode_leading_comma",
     {"-i", "/", "-o", "-", "--chmod=,a+r"},
     "invalid metadata option: empty chmod mode"},
    {"invalid_octal_mode",
     {"-i", "/", "-o", "-", "--chmod=1799"},
     "invalid metadata option: invalid octal chmod mode: 1799"},
    {"invalid_octal_mode_after_operation",
     {"-i", "/", "-o", "-", "--chmod=-1799"},
     "invalid metadata option: invalid octal chmod mode after operation: "
     "-1799"},
    {"whom_combined_with_octal_mode",
     {"-i", "/", "-o", "-", "--chmod=u+777"},
     "invalid metadata option: cannot combine whom with octal chmod mode: "
     "u+777"},
    {"missing_permissions",
     {"-i", "/", "-o", "-", "--chmod=u+"},
     "invalid metadata option: missing permissions in chmod mode: u+"},
    {"trailing_characters",
     {"-i", "/", "-o", "-", "--chmod=u+wpp"},
     "invalid metadata option: trailing characters in chmod mode: u+wpp"},
};

std::vector<cli_error_case> const filter_errors{
    {"cannot_combine_input_list_and_filter",
     {"--input-list", "-", "-o", "-", "-F", "+ *"},
     "cannot combine --input-list and --filter"},
    {"rules_must_start_with_plus_or_minus",
     {"-i", "/", "-o", "-", "-F", "% *"},
     "rules must start with + or -"},
    {"empty_filter_rule",
     {"-i", "/", "-o", "-", "-F", ""},
     "empty filter rule"},
    {"invalid_filter_rule",
     {"-i", "/", "-o", "-", "-F", "+i"},
     "invalid filter rule"},
    {"no_pattern_in_filter_rule",
     {"-i", "/", "-o", "-", "-F", "+  "},
     "no pattern in filter rule"},
    {"no_prefix_in_filter_rule",
     {"-i", "/", "-o", "-", "-F", " foo"},
     "no prefix in filter rule"},
    {"unknown_option_in_filter_rule",
     {"-i", "/", "-o", "-", "-F", "+x foo"},
     "unknown option 'x' in filter rule"},
    {"invalid_filter_debug_mode",
     {"-i", "/", "-o", "-", "--debug-filter=grmpf"},
     "invalid filter debug mode"},
};

#ifdef DWARFS_HAVE_RICEPP
std::vector<cli_error_case> const compression_errors{
    {"compression_cannot_be_used_without_category",
     {"-i", "/", "-o", "-", "-C", "ricepp"},
     "cannot be used without a category"},
    {"compression_cannot_be_used_for_category",
     {"-i", "/", "-o", "-", "--categorize", "-C", "incompressible::ricepp"},
     "cannot be used for category 'incompressible': "
     "metadata requirements not met"},
};
#endif

} // namespace

TEST_P(mkdwarfs_cli_error_test, fails_with_message) {
  auto const& c = GetParam();
  auto t = mkdwarfs_tester::create_empty();
  EXPECT_NE(0, t.run(c.args));
  EXPECT_THAT(t.err(), ::testing::HasSubstr(c.expected_error));
}

INSTANTIATE_TEST_SUITE_P(options, mkdwarfs_cli_error_test,
                         ::testing::ValuesIn(option_errors), cli_error_name{});

INSTANTIATE_TEST_SUITE_P(order, mkdwarfs_cli_error_test,
                         ::testing::ValuesIn(order_errors), cli_error_name{});

INSTANTIATE_TEST_SUITE_P(chmod, mkdwarfs_cli_error_test,
                         ::testing::ValuesIn(chmod_errors), cli_error_name{});

INSTANTIATE_TEST_SUITE_P(filter, mkdwarfs_cli_error_test,
                         ::testing::ValuesIn(filter_errors), cli_error_name{});

#ifdef DWARFS_HAVE_RICEPP
INSTANTIATE_TEST_SUITE_P(compression, mkdwarfs_cli_error_test,
                         ::testing::ValuesIn(compression_errors),
                         cli_error_name{});
#endif

// The remaining error cases don't fit the table: they either need extra setup
// or they exercise a file access failure rather than argument validation.

TEST(mkdwarfs_main_test, invalid_progress_mode) {
  mkdwarfs_tester t;
  t.iol->set_terminal_is_tty(true);
  t.iol->set_terminal_fancy(true);
  EXPECT_NE(0, t.run({"-i", "/", "-o", "-", "--progress=grmpf"}));
  EXPECT_THAT(t.err(), ::testing::HasSubstr("invalid progress mode"));
}

TEST(mkdwarfs_main_test, cannot_open_input_list_file) {
  mkdwarfs_tester t;
  EXPECT_NE(0, t.run({"--input-list", "missing.list", "-o", "-"}));
  EXPECT_THAT(t.err(), ::testing::HasSubstr("cannot open input list file"));
}

TEST(mkdwarfs_main_test, order_explicit_failed_to_open_file) {
  mkdwarfs_tester t;
  EXPECT_NE(
      0, t.run({"-i", "/", "-o", "-", "--order=explicit:file=explicit.txt"}));
  EXPECT_THAT(t.err(),
              ::testing::HasSubstr(
                  "failed to open explicit order file 'explicit.txt':"));
}

TEST(mkdwarfs_main_test, filesystem_header_error) {
  mkdwarfs_tester t;
  EXPECT_NE(0, t.run({"-i", "/", "-o", "-", "--header=header.txt"})) << t.err();
  EXPECT_THAT(t.err(), ::testing::HasSubstr("cannot open header file"));
}

TEST(mkdwarfs_test, input_must_be_a_directory) {
  mkdwarfs_tester t;
  EXPECT_NE(0, t.run({"-i", "/test.pl", "-o", "-"})) << t.err();
  EXPECT_THAT(t.err(), ::testing::HasSubstr("'/test.pl' must be a directory"));
}

TEST(mkdwarfs_test, output_file_exists) {
  mkdwarfs_tester t;
  t.fa->set_file("exists.dwarfs", "bla");
  EXPECT_NE(0, t.run({"-i", "/", "-o", "exists.dwarfs"})) << t.err();
  EXPECT_THAT(t.err(), ::testing::HasSubstr("output file already exists"));
}

TEST(mkdwarfs_test, output_file_force) {
  mkdwarfs_tester t;
  t.fa->set_file("exists.dwarfs", "bla");
  ASSERT_EQ(0, t.run({"-i", "/", "-o", "exists.dwarfs", "-l1", "--force"}))
      << t.err();
  auto fs = t.fs_from_file("exists.dwarfs");
  EXPECT_TRUE(fs.find("/foo.pl"));
}

TEST(mkdwarfs_test, output_not_opened_for_invalid_compression) {
  mkdwarfs_tester t;
  t.fa->set_file("exists.dwarfs", "original image");

  EXPECT_NE(0, t.run({"-i", "/", "-o", "exists.dwarfs", "--force", "-C",
                      "not-a-compressor"}));
  EXPECT_THAT(t.err(), ::testing::HasSubstr("failed to parse value"));
  EXPECT_EQ(0, t.fa->output_binary_open_count());
  auto const existing = t.fa->get_file("exists.dwarfs");
  ASSERT_TRUE(existing);
  EXPECT_EQ("original image", *existing);
}

#ifdef DWARFS_HAVE_RICEPP
TEST(mkdwarfs_test, output_not_opened_for_compression_metadata_requirements) {
  mkdwarfs_tester t;

  EXPECT_NE(0, t.run({"-i", "/", "-o", "test.dwarfs", "-C", "ricepp"}));
  EXPECT_THAT(t.err(), ::testing::HasSubstr("metadata requirements not met"));
  EXPECT_EQ(0, t.fa->output_binary_open_count());
}

TEST(mkdwarfs_test, output_not_opened_for_category_metadata_requirements) {
  mkdwarfs_tester t;

  EXPECT_NE(0, t.run({"-i", "/", "-o", "test.dwarfs", "--categorize", "-C",
                      "incompressible::ricepp"}));
  EXPECT_THAT(t.err(), ::testing::HasSubstr("metadata requirements not met"));
  EXPECT_EQ(0, t.fa->output_binary_open_count());
}
#endif

TEST(mkdwarfs_test, output_not_opened_for_invalid_superblock_options) {
  mkdwarfs_tester t;

  EXPECT_NE(0, t.run({"-i", "/", "-o", "test.dwarfs", "--no-backwards-compat",
                      "--label=test", "--clear-label"}));
  EXPECT_THAT(t.err(), ::testing::HasSubstr("mutually exclusive"));
  EXPECT_EQ(0, t.fa->output_binary_open_count());
  EXPECT_FALSE(t.fa->get_file("test.dwarfs"));
}

TEST(mkdwarfs_test, output_not_opened_for_invalid_rewrite_input) {
  mkdwarfs_tester t;

  EXPECT_NE(
      0, t.run({"-i", "missing.dwarfs", "-o", "test.dwarfs", "--recompress"}));
  EXPECT_THAT(t.err(), ::testing::HasSubstr("could not open input filesystem"));
  EXPECT_EQ(0, t.fa->output_binary_open_count());
  EXPECT_FALSE(t.fa->get_file("test.dwarfs"));
}

TEST(mkdwarfs_test, output_file_fail_open) {
  mkdwarfs_tester t;
  t.fa->set_file("exists.dwarfs", "bla");
  t.fa->set_open_error(
      "exists.dwarfs",
      std::make_error_code(std::errc::device_or_resource_busy));
  EXPECT_NE(0, t.run({"-i", "/", "-o", "exists.dwarfs", "--force"})) << t.err();
  EXPECT_THAT(t.err(), ::testing::HasSubstr("cannot open output file"));
}

TEST(mkdwarfs_test, output_file_fail_close) {
  mkdwarfs_tester t;
  t.fa->set_close_error("test.dwarfs",
                        std::make_error_code(std::errc::no_space_on_device));
  EXPECT_NE(0, t.run({"-i", "/", "-o", "test.dwarfs"})) << t.err();
  EXPECT_THAT(t.err(), ::testing::HasSubstr("failed to close output file"));
}

TEST(mkdwarfs_test, low_memory_limit) {
  {
    mkdwarfs_tester t;
    EXPECT_EQ(
        0, t.run("-i / -o - -l5 --log-level=warn -S 27 --num-workers=8 -L 1g"));
    EXPECT_THAT(t.err(),
                ::testing::Not(::testing::HasSubstr("low memory limit")));
  }

  {
    mkdwarfs_tester t;
    EXPECT_EQ(
        0, t.run("-i / -o - -l5 --log-level=warn -S 28 --num-workers=8 -L 1g"));
    EXPECT_THAT(t.err(), ::testing::HasSubstr("low memory limit"));
  }
}

TEST(mkdwarfs_test, block_number_out_of_range) {
  mkdwarfs_tester t;
  EXPECT_EQ(0, t.run({"-i", "/", "-o", "-", "-l4"})) << t.err();
  auto fs = t.fs_from_stdout();
  EXPECT_THAT(
      [&] {
        auto br [[maybe_unused]] = fs.read_raw_block_data(4711, 0, 1024).get();
      },
      ::testing::ThrowsMessage<runtime_error>(
          ::testing::HasSubstr("block number out of range")));
}

class mkdwarfs_log_mem_usage_test
    : public testing::TestWithParam<std::tuple<bool, std::string>> {};

TEST_P(mkdwarfs_log_mem_usage_test, log_memory_usage) {
  auto const [kAccurate, kProgress] = GetParam();

  std::string const mem_usage_file{"memusage.log"};
  mkdwarfs_tester t;
  t.os->setenv("DWARFS_LOG_MEMORY_USAGE", mem_usage_file);
  if (kAccurate) {
    t.os->setenv("DWARFS_ACCURATE_MEMORY_USAGE", "1");
  }
  EXPECT_EQ(0, t.run({"-i", "/", "-o", "-", "-l4", "--progress", kProgress}))
      << t.err();
  auto log = t.fa->get_file(mem_usage_file);
  ASSERT_TRUE(log);

  auto const lines = split_to<std::vector<std::string_view>>(*log, '\n');
  EXPECT_GT(lines.size(), 2);

  auto const title =
      split_to<std::vector<std::string_view>>(lines.front(), '\t');
  EXPECT_THAT(title, ::testing::ElementsAre("time", "total", "anon", "file",
                                            "swap", "allocated"));

  auto const row = split_to<std::vector<std::string_view>>(lines.at(1), '\t');
  EXPECT_EQ(row.size(), title.size());

  auto const time = to<double>(row[0]);
  auto const total = to<uint64_t>(row[1]);
  auto const anon = to<uint64_t>(row[2]);
  auto const file = to<uint64_t>(row[3]);
  auto const allocated = to<uint64_t>(row[5]);

  EXPECT_GT(time, 0.0);
  EXPECT_GT(total, 0);

#ifdef __linux__
  EXPECT_GT(anon, 0);
  EXPECT_GT(file, 0);
#else
  EXPECT_EQ(anon, 0);
  EXPECT_EQ(file, 0);
#endif

#ifdef DWARFS_USE_JEMALLOC
  bool jemalloc_has_stats = false;
  size_t sz = sizeof(jemalloc_has_stats);
  ::mallctl("config.stats", &jemalloc_has_stats, &sz, nullptr, 0);
  if (jemalloc_has_stats) {
    EXPECT_GT(allocated, 0);
  } else {
    EXPECT_EQ(allocated, 0);
  }
#else
  EXPECT_EQ(allocated, 0);
#endif
}

INSTANTIATE_TEST_SUITE_P(dwarfs, mkdwarfs_log_mem_usage_test,
                         ::testing::Combine(::testing::Bool(),
                                            ::testing::Values("none", "simple",
                                                              "ascii",
                                                              "unicode")));

TEST(mkdwarfs_test, estimate_compression_memory) {
  {
    auto t = mkdwarfs_tester::create_empty();
    ASSERT_EQ(0, t.run({"--estimate-compression-memory"})) << t.err();
    EXPECT_THAT(t.err(), ::testing::IsEmpty());
    EXPECT_THAT(
        t.out(),
        ::testing::AllOf(::testing::HasSubstr("will use up to"),
                         ::testing::HasSubstr(
                             "per worker thread to compress 16 MiB blocks")));
  }

  {
    auto t = mkdwarfs_tester::create_empty();
    ASSERT_EQ(0, t.run({"--estimate-compression-memory", "-S", "30", "-C",
                        "zstd:level=20"}))
        << t.err();
    EXPECT_THAT(t.err(), ::testing::IsEmpty());
    EXPECT_THAT(
        t.out(),
        ::testing::AllOf(::testing::HasSubstr("zstd [level=20] will use up to"),
                         ::testing::HasSubstr(
                             "per worker thread to compress 1 GiB blocks")));
  }
}

TEST(mkdwarfs_test, deprecated_file_hash_option) {
  {
    mkdwarfs_tester t;
    EXPECT_EQ(0, t.run({"-i", "/", "-o", "-", "-l4", "--file-hash=grmpf"}))
        << t.err();
    EXPECT_THAT(t.err(), ::testing::HasSubstr(
                             "the '--file-hash' option is deprecated and will "
                             "be removed in a future version"));
    EXPECT_THAT(t.err(), ::testing::Not(::testing::HasSubstr("--no-dedupe")));
  }

  {
    mkdwarfs_tester t;
    EXPECT_EQ(0, t.run({"-i", "/", "-o", "-", "-l4", "--file-hash=none"}))
        << t.err();
    EXPECT_THAT(t.err(), ::testing::HasSubstr(
                             "the '--file-hash' option is deprecated and will "
                             "be removed in a future version"));
    EXPECT_THAT(t.err(),
                ::testing::HasSubstr(
                    "use '--no-dedupe' instead of '--file-hash=none'"));
  }
}

TEST(mkdwarfs_test, recompress_refuses_to_overwrite_input) {
  mkdwarfs_tester t;
  auto const image = read_file(test_dir / "data.dwarfs");
  t.os->add_file("foo.dwarfs", image);
  t.fa->set_file("foo.dwarfs", image);
  EXPECT_NE(0, t.run({"-i", "foo.dwarfs", "-o", "foo.dwarfs", "--recompress",
                      "--force"}))
      << t.err();
  EXPECT_THAT(t.err(),
              ::testing::HasSubstr("refusing to overwrite input file system"));
}
