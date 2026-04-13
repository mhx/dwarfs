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

#include <unordered_set>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <dwarfs/writer/entry_storage.h>
#include <dwarfs/writer/inode_options.h>

#include <dwarfs/writer/internal/inode_manager.h>
#include <dwarfs/writer/internal/progress.h>
#include <dwarfs/writer/internal/provisional_entry.h>

#include "test_helpers.h"
#include "test_logger.h"

using namespace dwarfs;
namespace fs = std::filesystem;

namespace {

using writer::entry_storage;
using writer::entry_type;
using writer::internal::entry;
using writer::internal::inode_manager;
using writer::internal::progress;

using dwarfs::test::os_access_mock;
using dwarfs::test::test_logger;

fs::path make_root_path() {
#ifdef _WIN32
  return fs::path(std::wstring(1, fs::path::preferred_separator));
#else
  return fs::path(std::string(1, fs::path::preferred_separator));
#endif
}

struct recording_visitor final : writer::entry_handle_visitor {
  std::vector<std::string> events;

  void visit(writer::file_handle h) override {
    events.push_back("file:" + h.unix_dpath());
  }
  void visit(writer::device_handle h) override {
    events.push_back("device:" + h.unix_dpath());
  }
  void visit(writer::other_handle h) override {
    events.push_back("other:" + h.unix_dpath());
  }
  void visit(writer::link_handle h) override {
    events.push_back("link:" + h.unix_dpath());
  }
  void visit(writer::dir_handle h) override {
    events.push_back("dir:" + h.unix_dpath());
  }
};

} // namespace

struct entry_test : public ::testing::Test {
  fs::path sep{
#ifdef _WIN32
      std::wstring
#else
      std::string
#endif
      (1, fs::path::preferred_separator)};
  std::shared_ptr<os_access_mock> os;

  void SetUp() override { os = os_access_mock::create_test_instance(); }

  void TearDown() override { os.reset(); }

  writer::entry_handle
  create_entry(entry_storage& tree, os_access& osa, fs::path const& path) {
    return writer::internal::provisional_entry(osa, path).commit(tree);
  }

  writer::entry_handle
  create_entry(entry_storage& tree, os_access& osa, fs::path const& path,
               writer::entry_handle parent) {
    return writer::internal::provisional_entry(osa, path, parent).commit(tree);
  }

  writer::entry_handle create_entry(entry_storage& tree, fs::path const& path) {
    return create_entry(tree, *os, path);
  }

  writer::entry_handle create_entry(entry_storage& tree, fs::path const& path,
                                    writer::entry_handle parent) {
    return create_entry(tree, *os, path, parent);
  }
};

TEST_F(entry_test, path) {
  auto tree = entry_storage{};
  auto e1 = create_entry(tree, sep);
  auto e2 = create_entry(tree, sep / "somelink", e1);
  auto e3 = create_entry(tree, sep / "somedir", e1);
  auto e4 = create_entry(tree, sep / "somedir" / "ipsum.py", e3);

  EXPECT_FALSE(e1.has_parent());
  EXPECT_EQ(e1.type(), entry_type::E_DIR);
  EXPECT_TRUE(e1.is_dir());

  EXPECT_EQ(sep.string(), e1.name());
  EXPECT_EQ(sep, e1.fs_path());
  EXPECT_EQ(sep.string(), e1.path_as_string());
  EXPECT_EQ("/", e1.unix_dpath());

  EXPECT_TRUE(e2.has_parent());
  EXPECT_FALSE(e2.is_dir());
  EXPECT_EQ(e2.type(), entry_type::E_LINK);
  EXPECT_TRUE(e2.is_link());

  EXPECT_EQ("somelink", e2.name());
  EXPECT_EQ(sep / "somelink", e2.fs_path());
  EXPECT_EQ((sep / "somelink").string(), e2.path_as_string());
  EXPECT_EQ("/somelink", e2.unix_dpath());

  EXPECT_TRUE(e3.has_parent());
  EXPECT_EQ(e3.type(), entry_type::E_DIR);
  EXPECT_TRUE(e3.is_dir());

  EXPECT_EQ("somedir", e3.name());
  EXPECT_EQ(sep / "somedir", e3.fs_path());
  EXPECT_EQ((sep / "somedir").string(), e3.path_as_string());
  EXPECT_EQ("/somedir/", e3.unix_dpath());

  EXPECT_TRUE(e4.has_parent());
  EXPECT_FALSE(e4.is_dir());
  EXPECT_EQ(e4.type(), entry_type::E_FILE);
  EXPECT_TRUE(e4.is_file());

  EXPECT_EQ("ipsum.py", e4.name());
  EXPECT_EQ(sep / "somedir" / "ipsum.py", e4.fs_path());
  EXPECT_EQ((sep / "somedir" / "ipsum.py").string(), e4.path_as_string());
  EXPECT_EQ("/somedir/ipsum.py", e4.unix_dpath());
}

TEST_F(entry_test, factory_creates_expected_entry_kinds) {
  auto tree = entry_storage{};

  auto root = create_entry(tree, sep);
  ASSERT_TRUE(root);
  EXPECT_EQ(entry_type::E_DIR, root.type());
  EXPECT_TRUE(root.is_dir());

  auto test_pl = create_entry(tree, sep / "test.pl", root);
  ASSERT_TRUE(test_pl);
  EXPECT_EQ(entry_type::E_FILE, test_pl.type());
  EXPECT_FALSE(test_pl.is_dir());
  EXPECT_TRUE(test_pl.is_file());

  auto somelink = create_entry(tree, sep / "somelink", root);
  ASSERT_TRUE(somelink);
  EXPECT_EQ(entry_type::E_LINK, somelink.type());
  EXPECT_FALSE(somelink.is_dir());
  EXPECT_TRUE(somelink.is_link());

  auto somedir = create_entry(tree, sep / "somedir", root);
  ASSERT_TRUE(somedir);
  EXPECT_EQ(entry_type::E_DIR, somedir.type());
  EXPECT_TRUE(somedir.is_dir());

  auto ipsum_py = create_entry(tree, sep / "somedir" / "ipsum.py", somedir);
  ASSERT_TRUE(ipsum_py);
  EXPECT_EQ(entry_type::E_FILE, ipsum_py.type());
  EXPECT_FALSE(ipsum_py.is_dir());
  EXPECT_TRUE(ipsum_py.is_file());

  auto null_dev = create_entry(tree, sep / "somedir" / "null", somedir);
  ASSERT_TRUE(null_dev);
  EXPECT_EQ(entry_type::E_DEVICE, null_dev.type());
  EXPECT_FALSE(null_dev.is_dir());
  EXPECT_TRUE(null_dev.is_device());

  auto zero_dev = create_entry(tree, sep / "somedir" / "zero", somedir);
  ASSERT_TRUE(zero_dev);
  EXPECT_EQ(entry_type::E_DEVICE, zero_dev.type());
  EXPECT_FALSE(zero_dev.is_dir());
  EXPECT_TRUE(zero_dev.is_device());

  auto pipe = create_entry(tree, sep / "somedir" / "pipe", somedir);
  ASSERT_TRUE(pipe);
  EXPECT_EQ(entry_type::E_OTHER, pipe.type());
  EXPECT_FALSE(pipe.is_dir());
  EXPECT_TRUE(pipe.is_other());
}

TEST_F(entry_test, parent_roundtrip_and_less_revpath_work) {
  auto local_os = std::make_shared<os_access_mock>();
  auto root_path = make_root_path();

  local_os->add_dir(root_path);
  local_os->add_dir(root_path / "a");
  local_os->add_dir(root_path / "b");
  local_os->add_file(root_path / "a" / "x", "ax");
  local_os->add_file(root_path / "b" / "x", "bx");

  auto tree = entry_storage{};
  auto root = create_entry(tree, *local_os, root_path);
  auto a = create_entry(tree, *local_os, root_path / "a", root);
  auto b = create_entry(tree, *local_os, root_path / "b", root);
  auto ax = create_entry(tree, *local_os, root_path / "a" / "x", a);
  auto bx = create_entry(tree, *local_os, root_path / "b" / "x", b);

  ASSERT_TRUE(root);
  ASSERT_TRUE(a);
  ASSERT_TRUE(b);
  ASSERT_TRUE(ax);
  ASSERT_TRUE(bx);

  EXPECT_FALSE(root.has_parent());
  EXPECT_TRUE(a.has_parent());
  EXPECT_TRUE(ax.has_parent());

  EXPECT_EQ(root, a.parent());
  EXPECT_EQ(a, ax.parent());
  ASSERT_TRUE(ax.parent().parent());
  EXPECT_EQ(root, ax.parent().parent());

  EXPECT_TRUE(a.less_revpath(b));
  EXPECT_FALSE(b.less_revpath(a));

  // Same basename, different parents.
  EXPECT_TRUE(ax.less_revpath(bx));
  EXPECT_FALSE(bx.less_revpath(ax));
}

TEST_F(entry_test, walk_visits_preorder_in_insertion_order) {
  auto local_os = std::make_shared<os_access_mock>();
  auto root_path = make_root_path();

  local_os->add_dir(root_path);
  local_os->add_dir(root_path / "a");
  local_os->add_file(root_path / "b", "b");
  local_os->add_file(root_path / "a" / "c", "c");

  auto tree = entry_storage{};
  auto root = create_entry(tree, *local_os, root_path).as_dir();
  auto a = create_entry(tree, *local_os, root_path / "a", root).as_dir();
  auto b = create_entry(tree, *local_os, root_path / "b", root);
  auto c = create_entry(tree, *local_os, root_path / "a" / "c", a);

  ASSERT_TRUE(root);
  ASSERT_TRUE(a);
  ASSERT_TRUE(b);
  ASSERT_TRUE(c);

  tree.freeze();

  std::vector<std::string> visited;
  root.walk([&](writer::entry_handle e) { visited.push_back(e.unix_dpath()); });

  EXPECT_EQ((std::vector<std::string>{
                "/",
                "/a/",
                "/a/c",
                "/b",
            }),
            visited);
}

TEST_F(entry_test, accept_visits_dirs_pre_and_post_in_current_order) {
  auto local_os = std::make_shared<os_access_mock>();
  auto root_path = make_root_path();

  local_os->add_dir(root_path);
  local_os->add_dir(root_path / "a");
  local_os->add_file(root_path / "b", "b");
  local_os->add_file(root_path / "a" / "c", "c");

  auto tree = entry_storage{};
  auto root = create_entry(tree, *local_os, root_path).as_dir();
  auto a = create_entry(tree, *local_os, root_path / "a", root).as_dir();
  auto b = create_entry(tree, *local_os, root_path / "b", root);
  auto c = create_entry(tree, *local_os, root_path / "a" / "c", a);

  ASSERT_TRUE(root);
  ASSERT_TRUE(a);
  ASSERT_TRUE(b);
  ASSERT_TRUE(c);

  tree.freeze();

  recording_visitor pre;
  root.accept(pre, true);
  EXPECT_EQ((std::vector<std::string>{
                "dir:/",
                "dir:/a/",
                "file:/a/c",
                "file:/b",
            }),
            pre.events);

  recording_visitor post;
  root.accept(post, false);
  EXPECT_EQ((std::vector<std::string>{
                "file:/a/c",
                "dir:/a/",
                "file:/b",
                "dir:/",
            }),
            post.events);
}

TEST_F(entry_test, find_works_below_and_above_lookup_threshold) {
  {
    auto tree = entry_storage{};
    auto root = create_entry(tree, sep).as_dir();
    ASSERT_TRUE(root);

    create_entry(tree, sep / "somelink", root);
    create_entry(tree, sep / "somedir", root);
    create_entry(tree, sep / "test.pl", root);

    auto found_small = root.find("somelink");
    ASSERT_TRUE(found_small);
    EXPECT_EQ("somelink", found_small.name());
    EXPECT_EQ(entry_type::E_LINK, found_small.type());
    EXPECT_FALSE(root.find("does-not-exist").valid());
  }

  {
    // Synthetic large directory to force the lookup-table path.
    auto local_os = std::make_shared<os_access_mock>();
    auto root_path = make_root_path();

    local_os->add_dir(root_path);
    for (int i = 0; i < 20; ++i) {
      local_os->add_file(root_path / ("f" + std::to_string(i)), "x");
    }

    auto tree = entry_storage{};
    auto big_root = create_entry(tree, *local_os, root_path).as_dir();
    ASSERT_TRUE(big_root);

    for (int i = 0; i < 20; ++i) {
      create_entry(tree, *local_os, root_path / ("f" + std::to_string(i)),
                   big_root);
    }

    auto found_first = big_root.find("f0");
    auto found_mid = big_root.find("f11");
    auto found_last = big_root.find("f19");

    ASSERT_TRUE(found_first);
    ASSERT_TRUE(found_mid);
    ASSERT_TRUE(found_last);

    EXPECT_EQ("f0", found_first.name());
    EXPECT_EQ("f11", found_mid.name());
    EXPECT_EQ("f19", found_last.name());
    EXPECT_FALSE(big_root.find("f20"));
  }
}

TEST_F(entry_test,
       remove_empty_dirs_removes_recursively_and_keeps_nonempty_subtrees) {
  auto local_os = std::make_shared<os_access_mock>();
  auto root_path = make_root_path();

  local_os->add_dir(root_path);
  local_os->add_dir(root_path / "keep");
  local_os->add_file(root_path / "keep" / "file.txt", "payload");
  local_os->add_dir(root_path / "empty_a");
  local_os->add_dir(root_path / "nested");
  local_os->add_dir(root_path / "nested" / "empty_b");

  auto tree = entry_storage{};
  auto root = create_entry(tree, *local_os, root_path).as_dir();
  auto keep = create_entry(tree, *local_os, root_path / "keep", root).as_dir();
  auto keep_file =
      create_entry(tree, *local_os, root_path / "keep" / "file.txt", keep);
  auto empty_a =
      create_entry(tree, *local_os, root_path / "empty_a", root).as_dir();
  auto nested =
      create_entry(tree, *local_os, root_path / "nested", root).as_dir();
  auto empty_b =
      create_entry(tree, *local_os, root_path / "nested" / "empty_b", nested)
          .as_dir();

  ASSERT_TRUE(root);
  ASSERT_TRUE(keep);
  ASSERT_TRUE(keep_file);
  ASSERT_TRUE(empty_a);
  ASSERT_TRUE(nested);
  ASSERT_TRUE(empty_b);

  progress prog{};
  prog.dirs_scanned = 4;
  prog.dirs_found = 4;

  tree.remove_empty_dirs(prog);

  tree.freeze();

  std::vector<std::string> after;
  root.walk([&](writer::entry_handle e) { after.push_back(e.unix_dpath()); });
  EXPECT_EQ((std::vector<std::string>{
                "/",
                "/keep/",
                "/keep/file.txt",
            }),
            after);

  // We started with 4 non-root dirs: keep, empty_a, nested, empty_b.
  // empty_a, nested, and empty_b should be removed.
  EXPECT_EQ(1, prog.dirs_scanned);
  EXPECT_EQ(1, prog.dirs_found);
}

TEST_F(entry_test, link_scan_reads_link_target_and_updates_counters) {
  auto tree = entry_storage{};
  auto root = create_entry(tree, sep);
  ASSERT_TRUE(root);

  auto somelink = create_entry(tree, sep / "somelink", root).as_link();
  auto somedir = create_entry(tree, sep / "somedir", root).as_dir();
  auto bad = create_entry(tree, sep / "somedir" / "bad", somedir).as_link();

  ASSERT_TRUE(somelink);
  ASSERT_TRUE(somedir);
  ASSERT_TRUE(bad);

  progress prog1{};
  somelink.scan(*os, prog1);
  EXPECT_EQ("somedir/ipsum.py", somelink.linkname());
  EXPECT_EQ(somelink.size(), prog1.original_size);
  EXPECT_EQ(somelink.allocated_size(), prog1.allocated_original_size);
  EXPECT_EQ(somelink.size(), prog1.symlink_size);

  progress prog2{};
  bad.scan(*os, prog2);
  EXPECT_EQ("../foo", bad.linkname());
  EXPECT_EQ(bad.size(), prog2.original_size);
  EXPECT_EQ(bad.allocated_size(), prog2.allocated_original_size);
  EXPECT_EQ(bad.size(), prog2.symlink_size);
}

TEST_F(entry_test, root_dir_must_be_a_directory) {
  auto tree = entry_storage{};

  EXPECT_THAT([&] { create_entry(tree, sep / "somelink"); },
              testing::ThrowsMessage<dwarfs::runtime_error>(
                  testing::HasSubstr("must be a directory")));
}

TEST_F(entry_test, file_create_data_initializes_file_state) {
  auto tree = entry_storage{};

  auto root = create_entry(tree, sep);
  ASSERT_TRUE(root);

  auto empty = create_entry(tree, sep / "empty", root);
  ASSERT_TRUE(empty);

  auto f = empty.as_file();
  ASSERT_TRUE(f);

  f.create_data();

  EXPECT_FALSE(f.is_invalid());
  EXPECT_EQ(1, f.hardlink_count());
  EXPECT_FALSE(f.inode_num().has_value());
  EXPECT_TRUE(f.hash().empty());
}

TEST_F(entry_test, file_hardlink_shares_invalid_state) {
  auto tree = entry_storage{};

  auto root = create_entry(tree, sep);
  ASSERT_TRUE(root);

  auto foo_entry = create_entry(tree, sep / "foo.pl", root);
  auto bar_entry = create_entry(tree, sep / "bar.pl", root);
  ASSERT_TRUE(foo_entry);
  ASSERT_TRUE(bar_entry);

  auto foo = foo_entry.as_file();
  auto bar = bar_entry.as_file();
  ASSERT_TRUE(foo);
  ASSERT_TRUE(bar);

  foo.create_data();

  progress prog{};
  bar.hardlink(foo, prog);

  EXPECT_FALSE(foo.is_invalid());
  EXPECT_FALSE(bar.is_invalid());

  foo.set_invalid();

  EXPECT_TRUE(foo.is_invalid());
  EXPECT_TRUE(bar.is_invalid());
}

TEST_F(entry_test, file_hardlink_shares_inode_num_state) {
  auto tree = entry_storage{};

  auto root = create_entry(tree, sep);
  ASSERT_TRUE(root);

  auto foo_entry = create_entry(tree, sep / "foo.pl", root);
  auto bar_entry = create_entry(tree, sep / "bar.pl", root);
  ASSERT_TRUE(foo_entry);
  ASSERT_TRUE(bar_entry);

  auto foo = foo_entry.as_file();
  auto bar = bar_entry.as_file();
  ASSERT_TRUE(foo);
  ASSERT_TRUE(bar);

  foo.create_data();

  progress prog{};
  bar.hardlink(foo, prog);

  EXPECT_FALSE(foo.inode_num().has_value());
  EXPECT_FALSE(bar.inode_num().has_value());

  foo.set_inode_num(1234);

  ASSERT_TRUE(foo.inode_num().has_value());
  ASSERT_TRUE(bar.inode_num().has_value());
  EXPECT_EQ(1234, *foo.inode_num());
  EXPECT_EQ(1234, *bar.inode_num());
}

TEST_F(entry_test, file_hardlink_count_increments_and_is_shared) {
  auto tree = entry_storage{};

  auto root = create_entry(tree, sep);
  ASSERT_TRUE(root);

  auto foo_entry = create_entry(tree, sep / "foo.pl", root);
  auto bar_entry = create_entry(tree, sep / "bar.pl", root);
  ASSERT_TRUE(foo_entry);
  ASSERT_TRUE(bar_entry);

  auto foo = foo_entry.as_file();
  auto bar = bar_entry.as_file();
  ASSERT_TRUE(foo);
  ASSERT_TRUE(bar);

  foo.create_data();

  EXPECT_EQ(1, foo.hardlink_count());

  progress prog{};
  auto const foo_size = foo.size();
  auto const foo_allocated_size = foo.allocated_size();

  bar.hardlink(foo, prog);

  EXPECT_EQ(2, foo.hardlink_count());
  EXPECT_EQ(2, bar.hardlink_count());

  EXPECT_EQ(foo_size, prog.hardlink_size);
  EXPECT_EQ(foo_allocated_size, prog.allocated_hardlink_size);
  EXPECT_EQ(1, prog.hardlinks);
}

TEST_F(entry_test, file_hardlink_shares_hash_state) {
  auto tree = entry_storage{};

  auto root = create_entry(tree, sep);
  ASSERT_TRUE(root);

  auto foo_entry = create_entry(tree, sep / "foo.pl", root);
  auto bar_entry = create_entry(tree, sep / "bar.pl", root);
  ASSERT_TRUE(foo_entry);
  ASSERT_TRUE(bar_entry);

  auto foo = foo_entry.as_file();
  auto bar = bar_entry.as_file();
  ASSERT_TRUE(foo);
  ASSERT_TRUE(bar);

  foo.create_data();

  progress prog{};
  bar.hardlink(foo, prog);

  auto mm = os->open_file(sep / "foo.pl");
  foo.scan(mm, prog, "sha512-256");

  EXPECT_EQ(foo.hash(), bar.hash());
  EXPECT_FALSE(foo.hash().empty());
}

TEST_F(entry_test, file_inode_num_requires_data) {
  auto tree = entry_storage{};

  auto root = create_entry(tree, sep);
  ASSERT_TRUE(root);

  auto empty_entry = create_entry(tree, sep / "empty", root);
  ASSERT_TRUE(empty_entry);

  auto f = empty_entry.as_file();
  ASSERT_TRUE(f);

  ASSERT_DEATH(
      { [[maybe_unused]] auto const& ino = f.inode_num(); }, "file data unset");
}

TEST_F(entry_test, file_set_inode_num_requires_data) {
  auto tree = entry_storage{};

  auto root = create_entry(tree, sep);
  ASSERT_TRUE(root);

  auto empty_entry = create_entry(tree, sep / "empty", root);
  ASSERT_TRUE(empty_entry);

  auto f = empty_entry.as_file();
  ASSERT_TRUE(f);

  ASSERT_DEATH(f.set_inode_num(1), "file data unset");
}

TEST_F(entry_test, file_set_inode_num_only_once) {
  auto tree = entry_storage{};

  auto root = create_entry(tree, sep);
  ASSERT_TRUE(root);

  auto empty_entry = create_entry(tree, sep / "empty", root);
  ASSERT_TRUE(empty_entry);

  auto f = empty_entry.as_file();
  ASSERT_TRUE(f);

  f.create_data();
  f.set_inode_num(1234);

  ASSERT_DEATH(f.set_inode_num(5678),
               "attempt to set inode number more than once");
}

TEST_F(entry_test, file_set_inode_rejects_second_assignment) {
  auto tree = entry_storage{};

  auto root = create_entry(tree, sep);
  ASSERT_TRUE(root);

  auto empty_entry = create_entry(tree, sep / "empty", root);
  ASSERT_TRUE(empty_entry);

  auto f = empty_entry.as_file();
  ASSERT_TRUE(f);

  test_logger lgr;
  progress prog{};
  inode_manager im{lgr, prog, sep, {}, false};

  auto ino1 = im.create_inode();
  auto ino2 = im.create_inode();

  f.set_inode(ino1);

  ASSERT_DEATH(f.set_inode(ino2), "inode already set for file");
}

namespace {

struct entry_handle_test : entry_test {
  entry_storage storage;

  writer::entry_handle root;
  writer::file_handle file;
  writer::dir_handle somedir;
  writer::link_handle link;
  writer::device_handle dev;
  writer::other_handle other;
  writer::entry_handle nested_file;
  writer::entry_handle nested_dev;

  void SetUp() override {
    entry_test::SetUp();

    root = create_entry(storage, sep);
    ASSERT_TRUE(root);

    file = create_entry(storage, sep / "test.pl", root).as_file();
    somedir = create_entry(storage, sep / "somedir", root).as_dir();
    link = create_entry(storage, sep / "somelink", root).as_link();
    dev = create_entry(storage, sep / "somedir" / "null", somedir).as_device();
    other = create_entry(storage, sep / "somedir" / "pipe", somedir).as_other();
    nested_file = create_entry(storage, sep / "somedir" / "ipsum.py", somedir);
    nested_dev = create_entry(storage, sep / "somedir" / "null", somedir);

    ASSERT_TRUE(file);
    ASSERT_TRUE(somedir);
    ASSERT_TRUE(link);
    ASSERT_TRUE(dev);
    ASSERT_TRUE(other);
    ASSERT_TRUE(nested_file);
    ASSERT_TRUE(nested_dev);
  }

  void scan_link_target() {
    progress prog{};
    link.scan(*os, prog);
  }
};

} // namespace

TEST_F(entry_handle_test, typed_handles_convert_to_const_typed_handles) {
  scan_link_target();

  auto cfile = static_cast<writer::const_file_handle>(file);
  auto cdir = static_cast<writer::const_dir_handle>(somedir);
  auto clink = static_cast<writer::const_link_handle>(link);
  auto cdev = static_cast<writer::const_device_handle>(dev);
  auto cother = static_cast<writer::const_other_handle>(other);

  ASSERT_TRUE(cfile);
  ASSERT_TRUE(cdir);
  ASSERT_TRUE(clink);
  ASSERT_TRUE(cdev);
  ASSERT_TRUE(cother);

  EXPECT_EQ(file.name(), cfile.name());
  EXPECT_EQ(file.path_as_string(), cfile.path_as_string());

  EXPECT_EQ(somedir.name(), cdir.name());
  EXPECT_EQ("/somedir/", cdir.unix_dpath());

  EXPECT_EQ(link.name(), clink.name());
  EXPECT_EQ("somedir/ipsum.py", clink.linkname());

  EXPECT_EQ(dev.device_id(), cdev.device_id());
}

TEST_F(entry_handle_test, mutable_typed_handles_construct_const_entry_handles) {
  writer::const_entry_handle ce_file(file);
  writer::const_entry_handle ce_dir(somedir);
  writer::const_entry_handle ce_link(link);
  writer::const_entry_handle ce_dev(dev);
  writer::const_entry_handle ce_other(other);

  ASSERT_TRUE(ce_file);
  ASSERT_TRUE(ce_dir);
  ASSERT_TRUE(ce_link);
  ASSERT_TRUE(ce_dev);
  ASSERT_TRUE(ce_other);

  EXPECT_TRUE(ce_file.is_file());
  EXPECT_TRUE(ce_dir.is_dir());
  EXPECT_TRUE(ce_link.is_link());
  EXPECT_TRUE(ce_dev.is_device());
  EXPECT_TRUE(ce_other.is_other());

  EXPECT_FALSE(ce_file.is_dir());
  EXPECT_FALSE(ce_dir.is_file());
  EXPECT_FALSE(ce_link.is_device());
  EXPECT_FALSE(ce_dev.is_link());
  EXPECT_FALSE(ce_other.is_file());

  ASSERT_TRUE(ce_file.as_file());
  ASSERT_TRUE(ce_dir.as_dir());
  ASSERT_TRUE(ce_link.as_link());
  ASSERT_TRUE(ce_dev.as_device());
  ASSERT_TRUE(ce_other.as_other());

  EXPECT_FALSE(ce_file.as_link());
  EXPECT_FALSE(ce_link.as_file());
  EXPECT_FALSE(ce_dir.as_device());

  EXPECT_TRUE(ce_dev.as_device());
  EXPECT_FALSE(ce_other.as_device());
  EXPECT_FALSE(ce_dev.as_other());
  EXPECT_TRUE(ce_other.as_other());
}

TEST_F(entry_handle_test, const_handles_preserve_parent_information) {
  writer::const_entry_handle cf(nested_file);
  writer::const_entry_handle cd(nested_dev);

  ASSERT_TRUE(cf.has_parent());
  ASSERT_TRUE(cd.has_parent());

  auto file_parent = cf.parent();
  auto dev_parent = cd.parent();

  ASSERT_TRUE(file_parent);
  ASSERT_TRUE(dev_parent);

  EXPECT_TRUE(file_parent.is_dir());
  EXPECT_TRUE(dev_parent.is_dir());

  EXPECT_EQ("/somedir/", file_parent.unix_dpath());
  EXPECT_EQ("/somedir/", dev_parent.unix_dpath());

  ASSERT_TRUE(file_parent.parent());
  EXPECT_EQ("/", file_parent.parent().unix_dpath());
}

TEST_F(entry_handle_test, handle_hash_support_works_for_all_types) {
  std::unordered_set<writer::entry_handle> entries;
  entries.insert(root);
  entries.insert(root);
  EXPECT_EQ(1, entries.size());

  std::unordered_set<writer::file_handle> files;
  files.insert(file);
  files.insert(file);
  EXPECT_EQ(1, files.size());

  std::unordered_set<writer::dir_handle> dirs;
  dirs.insert(somedir);
  dirs.insert(somedir);
  EXPECT_EQ(1, dirs.size());

  std::unordered_set<writer::link_handle> links;
  links.insert(link);
  links.insert(link);
  EXPECT_EQ(1, links.size());

  std::unordered_set<writer::device_handle> devices;
  devices.insert(dev);
  devices.insert(dev);
  EXPECT_EQ(1, devices.size());

  std::unordered_set<writer::other_handle> others;
  others.insert(other);
  others.insert(other);
  EXPECT_EQ(1, others.size());
}
