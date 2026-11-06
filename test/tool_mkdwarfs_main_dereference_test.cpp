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

#include <set>
#include <string>
#include <vector>

#include <gmock/gmock.h>

#include "test_tool_main_tester.h"

using namespace dwarfs::test;
using namespace dwarfs;

namespace fs = std::filesystem;

TEST(mkdwarfs_test, dereference_symlinks) {
  auto t = mkdwarfs_tester::create_empty();
  t.os->add_dir("/");
  t.os->add_dir("/src");
  t.os->add_file("/src/original", "contents");
  t.os->add_dir("/blobs");
  t.os->add_file("/blobs/data", "external contents");
  t.os->add_dir("/external");
  t.os->add_file("/external/child", "directory contents");
  t.os->add_symlink("/src/local", "original");
  t.os->add_symlink("/src/remote", "/blobs/data");
  t.os->add_symlink("/src/dir", "/external");

  ASSERT_EQ(0, t.run({"-o", "deref.dwarfs", "-l1", "-i", "/src",
                      "--dereference=symlinks"}))
      << t.err();

  auto image = t.fs_from_file("deref.dwarfs");
  auto local = image.find("/local");
  auto remote = image.find("/remote");
  auto dir = image.find("/dir");
  auto child = image.find("/dir/child");

  ASSERT_TRUE(local);
  ASSERT_TRUE(remote);
  ASSERT_TRUE(dir);
  ASSERT_TRUE(child);
  EXPECT_TRUE(local->inode().is_regular_file());
  EXPECT_TRUE(remote->inode().is_regular_file());
  EXPECT_TRUE(dir->inode().is_directory());
  EXPECT_TRUE(child->inode().is_regular_file());
}

TEST(mkdwarfs_test, dereference_default_and_bare_option) {
  auto t = mkdwarfs_tester::create_empty();
  t.os->add_dir("/");
  t.os->add_dir("/src");
  t.os->add_file("/src/target", "data");
  t.os->add_symlink("/src/link", "target");

  ASSERT_EQ(0, t.run({"-o", "preserved.dwarfs", "-l1", "-i", "/src"}))
      << t.err();

  auto preserved = t.fs_from_file("preserved.dwarfs");

  ASSERT_TRUE(preserved.find("/link"));
  EXPECT_TRUE(preserved.find("/link")->inode().is_symlink());

  ASSERT_EQ(0,
            t.run({"-o", "bare.dwarfs", "-l1", "--dereference", "--", "/src/"}))
      << t.err();

  auto dereferenced = t.fs_from_file("bare.dwarfs");

  ASSERT_TRUE(dereferenced.find("/link"));
  EXPECT_TRUE(dereferenced.find("/link")->inode().is_regular_file());
}

TEST(mkdwarfs_test, dereference_external_symlinks) {
  auto t = mkdwarfs_tester::create_empty();
  t.os->add_dir("/");
  t.os->add_dir("/src");
  t.os->add_file("/src/original", "local");
  t.os->add_dir("/targets");
  t.os->add_dir("/targets/tree");
  t.os->add_dir("/targets/tree/sub");
  t.os->add_file("/targets/tree/sub/data", "inside");
  t.os->add_file("/targets/other", "outside");
  t.os->add_symlink("/src/internal", "original");
  t.os->add_symlink("/src/tree", "/targets/tree");
  t.os->add_symlink("/targets/tree/safe", "sub/data");
  t.os->add_symlink("/targets/tree/unsafe", "../other");
  t.os->add_symlink("/targets/tree/embedded", "sub/../sub/data");

  ASSERT_EQ(0, t.run({"-o", "external.dwarfs", "-l1", "-i", "/src",
                      "--dereference=external-symlinks"}))
      << t.err();

  auto image = t.fs_from_file("external.dwarfs");
  auto internal = image.find("/internal");
  auto tree = image.find("/tree");
  auto safe = image.find("/tree/safe");
  auto unsafe = image.find("/tree/unsafe");
  auto embedded = image.find("/tree/embedded");

  ASSERT_TRUE(internal);
  ASSERT_TRUE(tree);
  ASSERT_TRUE(safe);
  ASSERT_TRUE(unsafe);
  ASSERT_TRUE(embedded);
  EXPECT_TRUE(internal->inode().is_symlink());
  EXPECT_TRUE(tree->inode().is_directory());
  EXPECT_TRUE(safe->inode().is_symlink());
  EXPECT_TRUE(unsafe->inode().is_regular_file());
  EXPECT_TRUE(embedded->inode().is_regular_file());
}

TEST(mkdwarfs_test, dereferenced_files_are_not_implicit_hardlinks) {
  auto t = mkdwarfs_tester::create_empty();
  t.os->add_dir("/");
  t.os->add_dir("/src");
  t.os->add_dir("/blobs");
  t.os->add_file("/blobs/target", "shared", {.nlink = 2});
  t.os->add_symlink("/src/a", "/blobs/target");
  t.os->add_symlink("/src/b", "/blobs/target");

  ASSERT_EQ(0, t.run({"-o", "no-dedupe.dwarfs", "-l1", "-i", "/src",
                      "--dereference=symlinks", "--no-dedupe"}))
      << t.err();

  auto image = t.fs_from_file("no-dedupe.dwarfs");
  auto a = image.find("/a");
  auto b = image.find("/b");

  ASSERT_TRUE(a);
  ASSERT_TRUE(b);
  EXPECT_TRUE(a->inode().is_regular_file());
  EXPECT_TRUE(b->inode().is_regular_file());
  EXPECT_NE(a->inode().inode_num(), b->inode().inode_num());
}

TEST(mkdwarfs_test, dereference_input_list) {
  auto t = mkdwarfs_tester::create_empty();

  t.os->add_dir("/");
  t.os->add_dir("/src");
  t.os->add_dir("/outside");
  t.os->add_file("/outside/data", "contents");
  t.os->add_symlink("/src/alias", "/outside");

  auto const input = t.set_input_list(input_mode::from_file, "alias/data\n");

  ASSERT_EQ(0, t.run({"-o", "list-deref.dwarfs", "-l1", "-i", "/src",
                      "--input-list", input, "--dereference=symlinks"}))
      << t.err();

  auto image = t.fs_from_file("list-deref.dwarfs");
  auto data = image.find("/alias/data");

  ASSERT_TRUE(data);
  EXPECT_TRUE(data->inode().is_regular_file());
}

TEST(mkdwarfs_test, dereference_absolute_input_list_through_directory_symlink) {
  auto t = mkdwarfs_tester::create_empty();
  t.os->add_dir("/");
  t.os->add_dir("/src");
  t.os->add_dir("/outside");
  t.os->add_file("/outside/data", "contents");
  t.os->add_symlink("/src/alias", "/outside");

  auto const input =
      t.set_input_list(input_mode::from_file, "/src/alias/data\n");

  ASSERT_EQ(0, t.run({"-o", "absolute-alias.dwarfs", "-l1", "-i", "/src",
                      "--input-list", input, "--dereference=symlinks"}))
      << t.err();

  auto image = t.fs_from_file("absolute-alias.dwarfs");
  auto data = image.find("/alias/data");

  ASSERT_TRUE(data);
  EXPECT_TRUE(data->inode().is_regular_file());
}

TEST(mkdwarfs_test, absolute_input_list_normalizes_before_root_check) {
  auto t = mkdwarfs_tester::create_empty();
  t.os->add_dir("/");
  t.os->add_dir("/src");
  t.os->add_dir("/outside");
  t.os->add_file("/src/valid", "data");
  t.os->add_file("/outside/file", "outside");

  auto const input = t.set_input_list(input_mode::from_file,
                                      "/src/valid\n/src/../outside/file\n");

  ASSERT_EQ(0, t.run({"-o", "list-normalized.dwarfs", "-l1", "-i", "/src",
                      "--input-list", input}))
      << t.err();

  auto image = t.fs_from_file("list-normalized.dwarfs");

  EXPECT_TRUE(image.find("/valid"));
  EXPECT_FALSE(image.find("/outside/file"));
  EXPECT_THAT(t.err(), ::testing::HasSubstr("ignoring path"));
}

TEST(mkdwarfs_test, dereference_absolute_input_list) {
  auto t = mkdwarfs_tester::create_empty();
  t.os->add_dir("/");
  t.os->add_dir("/src");
  t.os->add_file("/src/target", "data");
  t.os->add_symlink("/src/link", "target");

  auto const input = t.set_input_list(input_mode::from_file, "/src/link\n");

  ASSERT_EQ(0, t.run({"-o", "listed.dwarfs", "-l1", "-i", "/src",
                      "--input-list", input, "--dereference=symlinks"}))
      << t.err();

  auto image = t.fs_from_file("listed.dwarfs");
  auto link = image.find("/link");

  ASSERT_TRUE(link);
  EXPECT_TRUE(link->inode().is_regular_file());
}

TEST(mkdwarfs_test, dereference_merged_directories) {
  auto t = mkdwarfs_tester::create_empty();
  t.os->add_dir("/");
  t.os->add_dir("/one");
  t.os->add_dir("/two");
  t.os->add_dir("/targets");
  t.os->add_dir("/targets/a");
  t.os->add_dir("/targets/b");
  t.os->add_dir("/targets/a/common");
  t.os->add_dir("/targets/b/common");
  t.os->add_file("/targets/a/common/first", "first");
  t.os->add_file("/targets/b/common/second", "second");
  t.os->add_symlink("/one/bar", "/targets/a");
  t.os->add_symlink("/two/bar", "/targets/b");

  ASSERT_EQ(0, t.run({"-o", "merged.dwarfs", "-l1", "--dereference=symlinks",
                      "--", "/one/", "/two/"}))
      << t.err();

  auto image = t.fs_from_file("merged.dwarfs");

  EXPECT_EQ((std::set<fs::path>{"", "bar", "bar/common", "bar/common/first",
                                "bar/common/second"}),
            get_all_fs_paths(image));
  EXPECT_TRUE(image.find("/bar/common/first")->inode().is_regular_file());
  EXPECT_TRUE(image.find("/bar/common/second")->inode().is_regular_file());
}

TEST(mkdwarfs_test, dereference_cycles_and_dangling_links) {
  auto t = mkdwarfs_tester::create_empty();
  t.os->add_dir("/");
  t.os->add_dir("/src");
  t.os->add_dir("/src/a");
  t.os->add_file("/src/a/file", "okay");
  t.os->add_symlink("/src/a/again", "/src");
  t.os->add_symlink("/src/dangling", "/missing");

  // A dangling link and a cycle are both reported as scan errors.
  ASSERT_EQ(2, t.run({"-o", "cycles.dwarfs", "-l1", "-i", "/src",
                      "--dereference=symlinks"}))
      << t.err();

  auto image = t.fs_from_file("cycles.dwarfs");

  EXPECT_TRUE(image.find("/a/file"));
  EXPECT_FALSE(image.find("/a/again"));
  EXPECT_FALSE(image.find("/dangling"));
  EXPECT_THAT(t.err(), ::testing::HasSubstr("directory cycle"));
  EXPECT_THAT(t.err(), ::testing::HasSubstr("error following entry"));
}

TEST(mkdwarfs_test, invalid_dereference_mode) {
  auto t = mkdwarfs_tester::create_empty();
  EXPECT_NE(0, t.run({"-o", "bad.dwarfs", "--dereference=hardlinks", "/src/"}));
  EXPECT_THAT(t.err(), ::testing::HasSubstr("invalid --dereference mode"));
}

TEST(mkdwarfs_test, dereference_cannot_be_used_when_recompressing) {
  auto t = mkdwarfs_tester::create_empty();

  EXPECT_EQ(1, t.run({"-i", "image.dwarfs", "-o", "out.dwarfs", "--recompress",
                      "--dereference=symlinks"}));
  EXPECT_THAT(t.err(), ::testing::HasSubstr(
                           "--dereference cannot be used when recompressing"));
}

TEST(mkdwarfs_test, dereference_named_file_and_directory_sources) {
  auto t = mkdwarfs_tester::create_empty();
  t.os->add_dir("/");
  t.os->add_dir("/src");
  t.os->add_dir("/outside");
  t.os->add_dir("/outside/tree");
  t.os->add_file("/outside/tree/child", "directory contents");
  t.os->add_file("/outside/data", "file contents");
  t.os->add_symlink("/src/dir-alias", "/outside/tree");
  t.os->add_symlink("/src/file-alias", "/outside/data");

  ASSERT_EQ(0, t.run({"-o", "named.dwarfs", "-l1", "--dereference=symlinks",
                      "--", "/src/dir-alias", "/src/file-alias"}))
      << t.err();

  auto image = t.fs_from_file("named.dwarfs");
  auto dir = image.find("/dir-alias");
  auto child = image.find("/dir-alias/child");
  auto file = image.find("/file-alias");

  ASSERT_TRUE(dir);
  ASSERT_TRUE(child);
  ASSERT_TRUE(file);
  EXPECT_TRUE(dir->inode().is_directory());
  EXPECT_TRUE(child->inode().is_regular_file());
  EXPECT_TRUE(file->inode().is_regular_file());
  EXPECT_FALSE(image.find("/tree"));
  EXPECT_FALSE(image.find("/data"));
}

TEST(mkdwarfs_test, dereference_external_symlinks_with_parent_components) {
  auto t = mkdwarfs_tester::create_empty();
  t.os->add_dir("/");
  t.os->add_dir("/src");
  t.os->add_dir("/src/sub");
  t.os->add_file("/src/data", "internal");
  t.os->add_dir("/outside");
  t.os->add_file("/outside/data", "external");
  t.os->add_symlink("/src/sub/inside", "../data");
  t.os->add_symlink("/src/sub/outside", "../../outside/data");

  ASSERT_EQ(0, t.run({"-o", "parent.dwarfs", "-l1", "-i", "/src",
                      "--dereference=external-symlinks"}))
      << t.err();

  auto image = t.fs_from_file("parent.dwarfs");
  auto inside = image.find("/sub/inside");
  auto outside = image.find("/sub/outside");

  ASSERT_TRUE(inside);
  ASSERT_TRUE(outside);
  EXPECT_TRUE(inside->inode().is_symlink());
  EXPECT_TRUE(outside->inode().is_regular_file());
}

TEST(mkdwarfs_test, dereference_external_symlinks_input_list_boundary) {
  auto t = mkdwarfs_tester::create_empty();
  t.os->add_dir("/");
  t.os->add_dir("/src");
  t.os->add_dir("/outside");
  t.os->add_dir("/outside/tree");
  t.os->add_file("/outside/tree/inside", "internal");
  t.os->add_file("/outside/data", "external");
  t.os->add_symlink("/src/alias", "/outside/tree");
  t.os->add_symlink("/outside/tree/safe", "inside");
  t.os->add_symlink("/outside/tree/unsafe", "../data");

  auto const input =
      t.set_input_list(input_mode::from_file, "alias/safe\nalias/unsafe\n");

  ASSERT_EQ(0,
            t.run({"-o", "list-boundary.dwarfs", "-l1", "-i", "/src",
                   "--input-list", input, "--dereference=external-symlinks"}))
      << t.err();

  auto image = t.fs_from_file("list-boundary.dwarfs");
  auto safe = image.find("/alias/safe");
  auto unsafe = image.find("/alias/unsafe");

  ASSERT_TRUE(safe);
  ASSERT_TRUE(unsafe);
  EXPECT_TRUE(safe->inode().is_symlink());
  EXPECT_TRUE(unsafe->inode().is_regular_file());
}

TEST(mkdwarfs_test, dereference_dangling_input_list_entries) {
  auto t = mkdwarfs_tester::create_empty();
  t.os->add_dir("/");
  t.os->add_dir("/src");
  t.os->add_symlink("/src/missing-dir", "/absent");
  t.os->add_symlink("/src/missing-file", "/also-absent");

  auto const input = t.set_input_list(input_mode::from_file,
                                      "missing-dir/child\nmissing-file\n");

  EXPECT_EQ(2, t.run({"-o", "dangling-list.dwarfs", "-l1", "-i", "/src",
                      "--input-list", input, "--dereference=symlinks"}))
      << t.err();

  EXPECT_THAT(t.err(),
              ::testing::HasSubstr("error following input-list directory"));
  EXPECT_THAT(t.err(),
              ::testing::HasSubstr("error following input-list entry"));
}

TEST(mkdwarfs_test, dereferenced_file_hash_failure_is_independent) {
  auto t = mkdwarfs_tester::create_empty();
  t.os->add_dir("/");
  t.os->add_dir("/src");
  t.os->add_dir("/outside");
  t.os->add_file("/outside/data", "contents");
  t.os->add_file("/src/other", "contents");
  t.os->add_symlink("/src/alias", "/outside/data");
  t.os->set_open_fail("/outside/data");

  // Equal file sizes force hashing, where the independently mapped file fails.
  EXPECT_EQ(2, t.run({"-o", "bad-hash.dwarfs", "-l1", "-i", "/src",
                      "--dereference=symlinks"}))
      << t.err();

  EXPECT_THAT(t.err(), ::testing::HasSubstr("failed to map file"));

  auto image = t.fs_from_file("bad-hash.dwarfs");
  auto alias = image.find("/alias");
  auto other = image.find("/other");

  ASSERT_TRUE(alias);
  ASSERT_TRUE(other);
  EXPECT_TRUE(alias->inode().is_regular_file());
  EXPECT_TRUE(other->inode().is_regular_file());
  EXPECT_NE(alias->inode().inode_num(), other->inode().inode_num());
}

TEST(mkdwarfs_test, dereference_merged_file_collision_and_relative_symlink) {
  // The symlink in /two/common refers to the *physical* second file,
  // even when /one/common/shared wins in the merged logical directory.
  for (bool reverse_sources : {false, true}) {
    SCOPED_TRACE(reverse_sources);

    for (std::string const& mode :
         {std::string{}, std::string{"--dereference=external-symlinks"},
          std::string{"--dereference=symlinks"}}) {
      SCOPED_TRACE(mode);

      auto t = mkdwarfs_tester::create_empty();
      t.os->add_dir("/");
      t.os->add_dir("/one");
      t.os->add_dir("/two");
      t.os->add_dir("/one/common");
      t.os->add_dir("/two/common");
      t.os->add_file("/one/common/shared", "FIRST");
      t.os->add_file("/two/common/shared", "SECOND");
      t.os->add_symlink("/two/common/alias", "shared");

      std::vector<std::string> args{"-o", "collision.dwarfs", "-l1"};
      if (!mode.empty()) {
        args.push_back(mode);
      }
      args.push_back("--");
      args.push_back(reverse_sources ? "/two/" : "/one/");
      args.push_back(reverse_sources ? "/one/" : "/two/");

      ASSERT_EQ(0, t.run(args)) << t.err();

      auto image = t.fs_from_file("collision.dwarfs");
      auto shared = image.find("/common/shared");
      auto alias = image.find("/common/alias");

      ASSERT_TRUE(shared);
      ASSERT_TRUE(alias);
      ASSERT_TRUE(shared->inode().is_regular_file());
      EXPECT_EQ(reverse_sources ? "SECOND" : "FIRST",
                image.read_string(shared->inode().inode_num()));
      EXPECT_THAT(t.err(), ::testing::HasSubstr("merging directory"));
      EXPECT_THAT(t.err(), ::testing::HasSubstr("duplicate entry"));

      if (mode == "--dereference=symlinks") {
        ASSERT_TRUE(alias->inode().is_regular_file());
        EXPECT_EQ("SECOND", image.read_string(alias->inode().inode_num()));
      } else {
        ASSERT_TRUE(alias->inode().is_symlink());
        EXPECT_EQ("shared",
                  image.readlink(alias->inode(), reader::readlink_mode::posix));
      }
    }
  }
}

TEST(mkdwarfs_test, dereference_merge_ordinary_and_symlinked_directory) {
  // Verify mapping propagation in both directions. The existing directory
  // may have either a normal source mapping or a self-anchored symlink mapping.
  for (bool symlink_source_first : {false, true}) {
    SCOPED_TRACE(symlink_source_first);

    auto t = mkdwarfs_tester::create_empty();
    t.os->add_dir("/");
    t.os->add_dir("/one");
    t.os->add_dir("/one/common",
                  {.mtim = file_stat::timespec_type{.sec = 111, .nsec = 0}});
    t.os->add_file("/one/common/shared", "ORDINARY");
    t.os->add_file("/one/common/ordinary-only", "ordinary child");
    t.os->add_dir("/two");
    t.os->add_dir("/targets");
    t.os->add_dir("/targets/linked",
                  {.mtim = file_stat::timespec_type{.sec = 222, .nsec = 0}});
    t.os->add_file("/targets/linked/shared", "LINKED");
    t.os->add_file("/targets/linked/linked-only", "linked child");
    t.os->add_symlink("/two/common", "/targets/linked");

    ASSERT_EQ(0, t.run({"-o", "mixed-merge.dwarfs", "-l1",
                        "--dereference=symlinks", "--keep-all-times", "--",
                        symlink_source_first ? "/two/" : "/one/",
                        symlink_source_first ? "/one/" : "/two/"}))
        << t.err();

    auto image = t.fs_from_file("mixed-merge.dwarfs");

    EXPECT_EQ(
        (std::set<fs::path>{"", "common", "common/shared",
                            "common/ordinary-only", "common/linked-only"}),
        get_all_fs_paths(image));

    auto common = image.find("/common");
    auto shared = image.find("/common/shared");
    auto ordinary = image.find("/common/ordinary-only");
    auto linked = image.find("/common/linked-only");

    ASSERT_TRUE(common);
    ASSERT_TRUE(shared);
    ASSERT_TRUE(ordinary);
    ASSERT_TRUE(linked);
    EXPECT_TRUE(common->inode().is_directory());
    EXPECT_EQ(symlink_source_first ? 222 : 111,
              image.getattr(common->inode()).mtime());
    EXPECT_EQ(symlink_source_first ? "LINKED" : "ORDINARY",
              image.read_string(shared->inode().inode_num()));
    EXPECT_EQ("ordinary child",
              image.read_string(ordinary->inode().inode_num()));
    EXPECT_EQ("linked child", image.read_string(linked->inode().inode_num()));
    EXPECT_THAT(t.err(), ::testing::HasSubstr("merging directory"));
    EXPECT_THAT(t.err(), ::testing::HasSubstr("duplicate entry"));
  }
}

TEST(mkdwarfs_test, dereference_external_preserves_dangling_internal_links) {
  auto t = mkdwarfs_tester::create_empty();
  t.os->add_dir("/");
  t.os->add_dir("/src");
  t.os->add_file("/src/valid", "okay");
  t.os->add_symlink("/src/internal", "missing");
  t.os->add_symlink("/src/external", "/missing");

  // Only the absolute, unsafe target is followed. An unresolved (but safe)
  // relative symlink must remain in the image.
  EXPECT_EQ(2, t.run({"-o", "dangling-external.dwarfs", "-l1", "-i", "/src",
                      "--dereference=external-symlinks"}))
      << t.err();

  auto image = t.fs_from_file("dangling-external.dwarfs");
  auto internal = image.find("/internal");
  auto valid = image.find("/valid");

  ASSERT_TRUE(internal);
  ASSERT_TRUE(valid);
  EXPECT_TRUE(internal->inode().is_symlink());
  EXPECT_EQ("missing",
            image.readlink(internal->inode(), reader::readlink_mode::posix));
  EXPECT_EQ("okay", image.read_string(valid->inode().inode_num()));
  EXPECT_FALSE(image.find("/external"));
  EXPECT_THAT(t.err(), ::testing::HasSubstr("error following entry"));
  EXPECT_THAT(t.err(), ::testing::HasSubstr("filesystem created with 1 error"));
}

TEST(mkdwarfs_test, dereference_symlink_chains_and_link_cycles) {
  auto t = mkdwarfs_tester::create_empty();
  t.os->add_dir("/");
  t.os->add_dir("/src");
  t.os->add_file("/src/target", "CHAIN");
  t.os->add_symlink("/src/middle", "target");
  t.os->add_symlink("/src/outer", "middle");
  t.os->add_symlink("/src/loop-a", "loop-b");
  t.os->add_symlink("/src/loop-b", "loop-a");

  EXPECT_EQ(2, t.run({"-o", "chains.dwarfs", "-l1", "-i", "/src",
                      "--dereference=symlinks"}))
      << t.err();

  auto image = t.fs_from_file("chains.dwarfs");

  for (auto const* path : {"/target", "/middle", "/outer"}) {
    auto file = image.find(path);
    ASSERT_TRUE(file) << path;
    ASSERT_TRUE(file->inode().is_regular_file()) << path;
    EXPECT_EQ("CHAIN", image.read_string(file->inode().inode_num())) << path;
  }

  EXPECT_FALSE(image.find("/loop-a"));
  EXPECT_FALSE(image.find("/loop-b"));
  EXPECT_THAT(t.err(), ::testing::HasSubstr("error following entry"));
  EXPECT_THAT(t.err(),
              ::testing::HasSubstr("filesystem created with 2 errors"));
}

TEST(mkdwarfs_test, dereference_file_directory_type_collisions) {
  for (bool directory_first : {false, true}) {
    SCOPED_TRACE(directory_first);

    auto t = mkdwarfs_tester::create_empty();
    t.os->add_dir("/");
    t.os->add_dir("/one");
    t.os->add_dir("/two");
    t.os->add_dir("/targets");
    t.os->add_dir("/targets/directory");
    t.os->add_file("/targets/directory/child", "CHILD");

    if (directory_first) {
      t.os->add_symlink("/one/item", "/targets/directory");
      t.os->add_file("/two/item", "FILE");
    } else {
      t.os->add_file("/one/item", "FILE");
      t.os->add_symlink("/two/item", "/targets/directory");
    }

    ASSERT_EQ(0, t.run({"-o", "type-collision.dwarfs", "-l1",
                        "--dereference=symlinks", "--", "/one/", "/two/"}))
        << t.err();

    auto image = t.fs_from_file("type-collision.dwarfs");
    auto item = image.find("/item");

    ASSERT_TRUE(item);

    if (directory_first) {
      EXPECT_TRUE(item->inode().is_directory());
      auto child = image.find("/item/child");
      ASSERT_TRUE(child);
      ASSERT_TRUE(child->inode().is_regular_file());
      EXPECT_EQ("CHILD", image.read_string(child->inode().inode_num()));
    } else {
      ASSERT_TRUE(item->inode().is_regular_file());
      EXPECT_EQ("FILE", image.read_string(item->inode().inode_num()));
      EXPECT_FALSE(image.find("/item/child"));
    }

    EXPECT_THAT(t.err(), ::testing::HasSubstr("duplicate entry"));
  }
}

// Files reached through different directory aliases must not acquire a shared
// output inode merely because their physical (dev, ino) values are identical.
TEST(mkdwarfs_test, dereference_directory_aliases_independent_files) {
  for (bool dedupe : {false, true}) {
    SCOPED_TRACE(dedupe);

    auto t = mkdwarfs_tester::create_empty();
    t.os->add_dir("/");
    t.os->add_dir("/src");
    t.os->add_dir("/target");
    t.os->add_file("/target/data", "ALIAS DATA");
    t.os->add_symlink("/src/a", "/target");
    t.os->add_symlink("/src/b", "/target");

    std::vector<std::string> args{"-o",   "aliases.dwarfs",        "-l1", "-i",
                                  "/src", "--dereference=symlinks"};
    if (!dedupe) {
      args.emplace_back("--no-dedupe");
    }

    ASSERT_EQ(0, t.run(args)) << t.err();

    auto image = t.fs_from_file("aliases.dwarfs");
    auto a = image.find("/a/data");
    auto b = image.find("/b/data");

    ASSERT_TRUE(a);
    ASSERT_TRUE(b);
    ASSERT_TRUE(a->inode().is_regular_file());
    ASSERT_TRUE(b->inode().is_regular_file());
    EXPECT_EQ("ALIAS DATA", image.read_string(a->inode().inode_num()));
    EXPECT_EQ("ALIAS DATA", image.read_string(b->inode().inode_num()));
    EXPECT_NE(a->inode().inode_num(), b->inode().inode_num());
  }
}

// Hardlinks inside each alias must remain hardlinks, but must not be joined to
// the corresponding group in another traversal of the same directory.
TEST(mkdwarfs_test, dereference_directory_aliases_scoped_hardlinks) {
  for (bool dedupe : {false, true}) {
    SCOPED_TRACE(dedupe);

    auto t = mkdwarfs_tester::create_empty();
    t.os->add_dir("/");
    t.os->add_dir("/src");
    t.os->add_dir("/target");
    t.os->add_file("/target/one", "HARDLINK DATA", {.ino = 500, .nlink = 2});
    t.os->add_file("/target/two", "HARDLINK DATA", {.ino = 500, .nlink = 2});
    t.os->add_symlink("/src/a", "/target");
    t.os->add_symlink("/src/b", "/target");

    std::vector<std::string> args{"-o",   "hardlinks.dwarfs",      "-l1", "-i",
                                  "/src", "--dereference=symlinks"};
    if (!dedupe) {
      args.emplace_back("--no-dedupe");
    }

    ASSERT_EQ(0, t.run(args)) << t.err();

    auto image = t.fs_from_file("hardlinks.dwarfs");
    auto a1 = image.find("/a/one");
    auto a2 = image.find("/a/two");
    auto b1 = image.find("/b/one");
    auto b2 = image.find("/b/two");

    ASSERT_TRUE(a1);
    ASSERT_TRUE(a2);
    ASSERT_TRUE(b1);
    ASSERT_TRUE(b2);
    EXPECT_EQ(a1->inode().inode_num(), a2->inode().inode_num());
    EXPECT_EQ(b1->inode().inode_num(), b2->inode().inode_num());
    EXPECT_NE(a1->inode().inode_num(), b1->inode().inode_num());
    EXPECT_EQ("HARDLINK DATA", image.read_string(a1->inode().inode_num()));
    EXPECT_EQ("HARDLINK DATA", image.read_string(b1->inode().inode_num()));
  }
}

TEST(mkdwarfs_test, dereference_ordinary_source_and_directory_alias) {
  auto t = mkdwarfs_tester::create_empty();
  t.os->add_dir("/");
  t.os->add_dir("/src");
  t.os->add_dir("/target");
  t.os->add_file("/target/data", "ORIGINAL");
  t.os->add_symlink("/src/alias", "/target");

  ASSERT_EQ(0, t.run({"-o", "ordinary-and-alias.dwarfs", "-l1", "--no-dedupe",
                      "--dereference=symlinks", "--", "/target/", "/src/"}))
      << t.err();

  auto image = t.fs_from_file("ordinary-and-alias.dwarfs");
  auto direct = image.find("/data");
  auto alias = image.find("/alias/data");

  ASSERT_TRUE(direct);
  ASSERT_TRUE(alias);
  EXPECT_EQ("ORIGINAL", image.read_string(direct->inode().inode_num()));
  EXPECT_EQ("ORIGINAL", image.read_string(alias->inode().inode_num()));
  EXPECT_NE(direct->inode().inode_num(), alias->inode().inode_num());
}

// A preserved symlink can itself have hardlinks. The non-file hardlink
// handler must use the same identity scope as for regular-file handling.
TEST(mkdwarfs_test, dereference_directory_aliases_scoped_symlink_hardlinks) {
  auto t = mkdwarfs_tester::create_empty();
  t.os->add_dir("/");
  t.os->add_dir("/src");
  t.os->add_dir("/target");
  t.os->add_file("/target/data", "DATA");
  t.os->add_symlink("/target/one", "data", {.ino = 600, .nlink = 2});
  t.os->add_symlink("/target/two", "data", {.ino = 600, .nlink = 2});
  t.os->add_symlink("/src/a", "/target");
  t.os->add_symlink("/src/b", "/target");

  ASSERT_EQ(0, t.run({"-o", "link-hardlinks.dwarfs", "-l1", "-i", "/src",
                      "--dereference=external-symlinks"}))
      << t.err();

  auto image = t.fs_from_file("link-hardlinks.dwarfs");
  auto a1 = image.find("/a/one");
  auto a2 = image.find("/a/two");
  auto b1 = image.find("/b/one");
  auto b2 = image.find("/b/two");

  ASSERT_TRUE(a1);
  ASSERT_TRUE(a2);
  ASSERT_TRUE(b1);
  ASSERT_TRUE(b2);
  EXPECT_TRUE(a1->inode().is_symlink());
  EXPECT_TRUE(b1->inode().is_symlink());
  EXPECT_EQ(a1->inode().inode_num(), a2->inode().inode_num());
  EXPECT_EQ(b1->inode().inode_num(), b2->inode().inode_num());
  EXPECT_NE(a1->inode().inode_num(), b1->inode().inode_num());
  EXPECT_EQ("data", image.readlink(a1->inode(), reader::readlink_mode::posix));
  EXPECT_EQ("data", image.readlink(b1->inode(), reader::readlink_mode::posix));
}

TEST(mkdwarfs_test, dereference_input_list_aliases_have_distinct_inodes) {
  auto t = mkdwarfs_tester::create_empty();
  t.os->add_dir("/");
  t.os->add_dir("/src");
  t.os->add_dir("/target");
  t.os->add_file("/target/data", "LIST DATA");
  t.os->add_symlink("/src/a", "/target");
  t.os->add_symlink("/src/b", "/target");

  auto const input =
      t.set_input_list(input_mode::from_file, "a/data\nb/data\n");

  ASSERT_EQ(0, t.run({"-o", "listed-aliases.dwarfs", "-l1", "-i", "/src",
                      "--input-list", input, "--no-dedupe",
                      "--dereference=symlinks"}))
      << t.err();

  auto image = t.fs_from_file("listed-aliases.dwarfs");
  auto a = image.find("/a/data");
  auto b = image.find("/b/data");

  ASSERT_TRUE(a);
  ASSERT_TRUE(b);
  EXPECT_EQ("LIST DATA", image.read_string(a->inode().inode_num()));
  EXPECT_EQ("LIST DATA", image.read_string(b->inode().inode_num()));
  EXPECT_NE(a->inode().inode_num(), b->inode().inode_num());
}
