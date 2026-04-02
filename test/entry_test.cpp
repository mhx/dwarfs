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

#include <dwarfs/writer/entry_factory.h>
#include <dwarfs/writer/entry_storage.h>
#include <dwarfs/writer/inode_options.h>

#include <dwarfs/writer/internal/entry.h>
#include <dwarfs/writer/internal/inode_manager.h>
#include <dwarfs/writer/internal/progress.h>

#include "test_helpers.h"
#include "test_logger.h"

using namespace dwarfs;
namespace fs = std::filesystem;

namespace {

using entry = writer::internal::entry;
using entry_type = writer::entry_type;
using entry_visitor = writer::internal::entry_visitor;
using progress = writer::internal::progress;

fs::path make_root_path() {
#ifdef _WIN32
  return fs::path(std::wstring(1, fs::path::preferred_separator));
#else
  return fs::path(std::string(1, fs::path::preferred_separator));
#endif
}

struct recording_visitor final : entry_visitor {
  std::vector<std::string> events;

  void visit(writer::internal::file* p) override {
    events.push_back("file:" + p->unix_dpath());
  }
  void visit(writer::internal::device* p) override {
    events.push_back("device:" + p->unix_dpath());
  }
  void visit(writer::internal::link* p) override {
    events.push_back("link:" + p->unix_dpath());
  }
  void visit(writer::internal::dir* p) override {
    events.push_back("dir:" + p->unix_dpath());
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
  std::shared_ptr<test::os_access_mock> os;
  std::optional<writer::entry_factory> ef;

  void SetUp() override {
    os = test::os_access_mock::create_test_instance();
    ef.emplace();
  }

  void TearDown() override {
    ef.reset();
    os.reset();
  }
};

TEST_F(entry_test, path) {
  auto tree = writer::entry_storage{};
  auto e1 = ef->create(tree, *os, sep);
  auto e2 = ef->create(tree, *os, sep / "somelink", e1);
  auto e3 = ef->create(tree, *os, sep / "somedir", e1);
  auto e4 = ef->create(tree, *os, sep / "somedir" / "ipsum.py", e3);

  EXPECT_FALSE(e1->has_parent());
  EXPECT_TRUE(e1->is_directory());
  EXPECT_EQ(e1->type(), entry_type::E_DIR);
  EXPECT_TRUE(e1->is_dir());

  EXPECT_EQ(sep.string(), e1->name());
  EXPECT_EQ(sep, e1->fs_path());
  EXPECT_EQ(sep.string(), e1->path_as_string());
  EXPECT_EQ("/", e1->unix_dpath());

  EXPECT_TRUE(e2->has_parent());
  EXPECT_FALSE(e2->is_directory());
  EXPECT_EQ(e2->type(), entry_type::E_LINK);
  EXPECT_TRUE(e2->is_link());

  EXPECT_EQ("somelink", e2->name());
  EXPECT_EQ(sep / "somelink", e2->fs_path());
  EXPECT_EQ((sep / "somelink").string(), e2->path_as_string());
  EXPECT_EQ("/somelink", e2->unix_dpath());

  EXPECT_TRUE(e3->has_parent());
  EXPECT_TRUE(e3->is_directory());
  EXPECT_EQ(e3->type(), entry_type::E_DIR);
  EXPECT_TRUE(e3->is_dir());

  EXPECT_EQ("somedir", e3->name());
  EXPECT_EQ(sep / "somedir", e3->fs_path());
  EXPECT_EQ((sep / "somedir").string(), e3->path_as_string());
  EXPECT_EQ("/somedir/", e3->unix_dpath());

  EXPECT_TRUE(e4->has_parent());
  EXPECT_FALSE(e4->is_directory());
  EXPECT_EQ(e4->type(), entry_type::E_FILE);
  EXPECT_TRUE(e4->is_file());

  EXPECT_EQ("ipsum.py", e4->name());
  EXPECT_EQ(sep / "somedir" / "ipsum.py", e4->fs_path());
  EXPECT_EQ((sep / "somedir" / "ipsum.py").string(), e4->path_as_string());
  EXPECT_EQ("/somedir/ipsum.py", e4->unix_dpath());
}

TEST_F(entry_test, factory_creates_expected_entry_kinds) {
  auto tree = writer::entry_storage{};

  auto root = ef->create(tree, *os, sep);
  ASSERT_TRUE(root);
  EXPECT_EQ(entry_type::E_DIR, root->type());
  EXPECT_TRUE(root->is_directory());
  EXPECT_TRUE(root->is_dir());

  auto test_pl = ef->create(tree, *os, sep / "test.pl", root);
  ASSERT_TRUE(test_pl);
  EXPECT_EQ(entry_type::E_FILE, test_pl->type());
  EXPECT_FALSE(test_pl->is_directory());
  EXPECT_TRUE(test_pl->is_file());

  auto somelink = ef->create(tree, *os, sep / "somelink", root);
  ASSERT_TRUE(somelink);
  EXPECT_EQ(entry_type::E_LINK, somelink->type());
  EXPECT_FALSE(somelink->is_directory());
  EXPECT_TRUE(somelink->is_link());

  auto somedir = ef->create(tree, *os, sep / "somedir", root);
  ASSERT_TRUE(somedir);
  EXPECT_EQ(entry_type::E_DIR, somedir->type());
  EXPECT_TRUE(somedir->is_directory());
  EXPECT_TRUE(somedir->is_dir());

  auto ipsum_py = ef->create(tree, *os, sep / "somedir" / "ipsum.py", somedir);
  ASSERT_TRUE(ipsum_py);
  EXPECT_EQ(entry_type::E_FILE, ipsum_py->type());
  EXPECT_FALSE(ipsum_py->is_directory());
  EXPECT_TRUE(ipsum_py->is_file());

  auto null_dev = ef->create(tree, *os, sep / "somedir" / "null", somedir);
  ASSERT_TRUE(null_dev);
  EXPECT_EQ(entry_type::E_DEVICE, null_dev->type());
  EXPECT_FALSE(null_dev->is_directory());
  EXPECT_TRUE(null_dev->is_device());

  auto zero_dev = ef->create(tree, *os, sep / "somedir" / "zero", somedir);
  ASSERT_TRUE(zero_dev);
  EXPECT_EQ(entry_type::E_DEVICE, zero_dev->type());
  EXPECT_FALSE(zero_dev->is_directory());
  EXPECT_TRUE(zero_dev->is_device());

  auto pipe = ef->create(tree, *os, sep / "somedir" / "pipe", somedir);
  ASSERT_TRUE(pipe);
  EXPECT_EQ(entry_type::E_OTHER, pipe->type());
  EXPECT_FALSE(pipe->is_directory());
  EXPECT_TRUE(pipe->is_other());
}

TEST_F(entry_test, parent_roundtrip_and_less_revpath_work) {
  auto local_os = std::make_shared<test::os_access_mock>();
  writer::entry_factory local_ef;
  auto root_path = make_root_path();

  local_os->add_dir(root_path);
  local_os->add_dir(root_path / "a");
  local_os->add_dir(root_path / "b");
  local_os->add_file(root_path / "a" / "x", "ax");
  local_os->add_file(root_path / "b" / "x", "bx");

  auto tree = writer::entry_storage{};
  auto root = local_ef.create(tree, *local_os, root_path);
  auto a = local_ef.create(tree, *local_os, root_path / "a", root);
  auto b = local_ef.create(tree, *local_os, root_path / "b", root);
  auto ax = local_ef.create(tree, *local_os, root_path / "a" / "x", a);
  auto bx = local_ef.create(tree, *local_os, root_path / "b" / "x", b);

  ASSERT_TRUE(root);
  ASSERT_TRUE(a);
  ASSERT_TRUE(b);
  ASSERT_TRUE(ax);
  ASSERT_TRUE(bx);

  EXPECT_FALSE(root->has_parent());
  EXPECT_TRUE(a->has_parent());
  EXPECT_TRUE(ax->has_parent());

  EXPECT_EQ(root, a->parent());
  EXPECT_EQ(a, ax->parent());
  ASSERT_TRUE(ax->parent()->parent());
  EXPECT_EQ(root, ax->parent()->parent());

  EXPECT_TRUE(a->less_revpath(*b));
  EXPECT_FALSE(b->less_revpath(*a));

  // Same basename, different parents.
  EXPECT_TRUE(ax->less_revpath(*bx));
  EXPECT_FALSE(bx->less_revpath(*ax));
}

TEST_F(entry_test, walk_visits_preorder_in_insertion_order) {
  auto local_os = std::make_shared<test::os_access_mock>();
  writer::entry_factory local_ef;
  auto root_path = make_root_path();

  local_os->add_dir(root_path);
  local_os->add_dir(root_path / "a");
  local_os->add_file(root_path / "b", "b");
  local_os->add_file(root_path / "a" / "c", "c");

  auto tree = writer::entry_storage{};
  auto root = local_ef.create(tree, *local_os, root_path)->as_dir();
  auto a = local_ef.create(tree, *local_os, root_path / "a", root)->as_dir();
  auto b = local_ef.create(tree, *local_os, root_path / "b", root);
  auto c = local_ef.create(tree, *local_os, root_path / "a" / "c", a);

  ASSERT_TRUE(root);
  ASSERT_TRUE(a);
  ASSERT_TRUE(b);
  ASSERT_TRUE(c);

  root->add(a);
  root->add(b);
  a->add(c);

  std::vector<std::string> visited;
  root->walk([&](entry* e) { visited.push_back(e->unix_dpath()); });

  EXPECT_EQ((std::vector<std::string>{
                "/",
                "/a/",
                "/a/c",
                "/b",
            }),
            visited);
}

TEST_F(entry_test, accept_visits_dirs_pre_and_post_in_current_order) {
  auto local_os = std::make_shared<test::os_access_mock>();
  writer::entry_factory local_ef;
  auto root_path = make_root_path();

  local_os->add_dir(root_path);
  local_os->add_dir(root_path / "a");
  local_os->add_file(root_path / "b", "b");
  local_os->add_file(root_path / "a" / "c", "c");

  auto tree = writer::entry_storage{};
  auto root = local_ef.create(tree, *local_os, root_path)->as_dir();
  auto a = local_ef.create(tree, *local_os, root_path / "a", root)->as_dir();
  auto b = local_ef.create(tree, *local_os, root_path / "b", root);
  auto c = local_ef.create(tree, *local_os, root_path / "a" / "c", a);

  ASSERT_TRUE(root);
  ASSERT_TRUE(a);
  ASSERT_TRUE(b);
  ASSERT_TRUE(c);

  root->add(a);
  root->add(b);
  a->add(c);

  recording_visitor pre;
  root->accept(pre, true);
  EXPECT_EQ((std::vector<std::string>{
                "dir:/",
                "dir:/a/",
                "file:/a/c",
                "file:/b",
            }),
            pre.events);

  recording_visitor post;
  root->accept(post, false);
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
    auto tree = writer::entry_storage{};
    auto root = ef->create(tree, *os, sep)->as_dir();
    ASSERT_TRUE(root);

    auto somelink = ef->create(tree, *os, sep / "somelink", root);
    auto somedir = ef->create(tree, *os, sep / "somedir", root);
    auto test_pl = ef->create(tree, *os, sep / "test.pl", root);

    root->add(somelink);
    root->add(somedir);
    root->add(test_pl);

    auto found_small = root->find("somelink");
    ASSERT_TRUE(found_small);
    EXPECT_EQ("somelink", found_small->name());
    EXPECT_EQ(entry_type::E_LINK, found_small->type());
    EXPECT_EQ(nullptr, root->find("does-not-exist"));
  }

  {
    // Synthetic large directory to force the lookup-table path.
    auto local_os = std::make_shared<test::os_access_mock>();
    writer::entry_factory local_ef;
    auto root_path = make_root_path();

    local_os->add_dir(root_path);
    for (int i = 0; i < 20; ++i) {
      local_os->add_file(root_path / ("f" + std::to_string(i)), "x");
    }

    auto tree = writer::entry_storage{};
    auto big_root = local_ef.create(tree, *local_os, root_path)->as_dir();
    ASSERT_TRUE(big_root);

    for (int i = 0; i < 20; ++i) {
      big_root->add(local_ef.create(
          tree, *local_os, root_path / ("f" + std::to_string(i)), big_root));
    }

    auto found_first = big_root->find("f0");
    auto found_mid = big_root->find("f11");
    auto found_last = big_root->find("f19");

    ASSERT_TRUE(found_first);
    ASSERT_TRUE(found_mid);
    ASSERT_TRUE(found_last);

    EXPECT_EQ("f0", found_first->name());
    EXPECT_EQ("f11", found_mid->name());
    EXPECT_EQ("f19", found_last->name());
    EXPECT_EQ(nullptr, big_root->find("f20"));

    // Also exercise behavior after sorting.
    big_root->sort();
    auto found_after_sort = big_root->find("f11");
    ASSERT_TRUE(found_after_sort);
    EXPECT_EQ("f11", found_after_sort->name());
  }
}

TEST_F(entry_test,
       remove_empty_dirs_removes_recursively_and_keeps_nonempty_subtrees) {
  auto local_os = std::make_shared<test::os_access_mock>();
  writer::entry_factory local_ef;
  auto root_path = make_root_path();

  local_os->add_dir(root_path);
  local_os->add_dir(root_path / "keep");
  local_os->add_file(root_path / "keep" / "file.txt", "payload");
  local_os->add_dir(root_path / "empty_a");
  local_os->add_dir(root_path / "nested");
  local_os->add_dir(root_path / "nested" / "empty_b");

  auto tree = writer::entry_storage{};
  auto root = local_ef.create(tree, *local_os, root_path)->as_dir();
  auto keep =
      local_ef.create(tree, *local_os, root_path / "keep", root)->as_dir();
  auto keep_file =
      local_ef.create(tree, *local_os, root_path / "keep" / "file.txt", keep);
  auto empty_a =
      local_ef.create(tree, *local_os, root_path / "empty_a", root)->as_dir();
  auto nested =
      local_ef.create(tree, *local_os, root_path / "nested", root)->as_dir();
  auto empty_b =
      local_ef
          .create(tree, *local_os, root_path / "nested" / "empty_b", nested)
          ->as_dir();

  ASSERT_TRUE(root);
  ASSERT_TRUE(keep);
  ASSERT_TRUE(keep_file);
  ASSERT_TRUE(empty_a);
  ASSERT_TRUE(nested);
  ASSERT_TRUE(empty_b);

  root->add(keep);
  root->add(empty_a);
  root->add(nested);
  keep->add(keep_file);
  nested->add(empty_b);

  std::vector<std::string> before;
  root->walk([&](entry* e) { before.push_back(e->unix_dpath()); });
  EXPECT_EQ((std::vector<std::string>{
                "/",
                "/keep/",
                "/keep/file.txt",
                "/empty_a/",
                "/nested/",
                "/nested/empty_b/",
            }),
            before);

  progress prog{};
  prog.dirs_scanned = 4;
  prog.dirs_found = 4;

  root->remove_empty_dirs(prog);

  std::vector<std::string> after;
  root->walk([&](entry* e) { after.push_back(e->unix_dpath()); });
  EXPECT_EQ((std::vector<std::string>{
                "/",
                "/keep/",
                "/keep/file.txt",
            }),
            after);

  ASSERT_TRUE(root->find("keep"));
  EXPECT_EQ(nullptr, root->find("empty_a"));
  EXPECT_EQ(nullptr, root->find("nested"));

  // We started with 4 non-root dirs: keep, empty_a, nested, empty_b.
  // empty_a, nested, and empty_b should be removed.
  EXPECT_EQ(1, prog.dirs_scanned);
  EXPECT_EQ(1, prog.dirs_found);
}

TEST_F(entry_test, link_scan_reads_link_target_and_updates_counters) {
  auto tree = writer::entry_storage{};
  auto root = ef->create(tree, *os, sep);
  ASSERT_TRUE(root);

  auto somelink = ef->create(tree, *os, sep / "somelink", root)->as_link();
  auto somedir = ef->create(tree, *os, sep / "somedir", root)->as_dir();
  auto bad = ef->create(tree, *os, sep / "somedir" / "bad", somedir)->as_link();

  ASSERT_TRUE(somelink);
  ASSERT_TRUE(somedir);
  ASSERT_TRUE(bad);

  progress prog1{};
  somelink->scan(*os, prog1);
  EXPECT_EQ("somedir/ipsum.py", somelink->linkname());
  EXPECT_EQ(somelink->size(), prog1.original_size);
  EXPECT_EQ(somelink->allocated_size(), prog1.allocated_original_size);
  EXPECT_EQ(somelink->size(), prog1.symlink_size);

  progress prog2{};
  bad->scan(*os, prog2);
  EXPECT_EQ("../foo", bad->linkname());
  EXPECT_EQ(bad->size(), prog2.original_size);
  EXPECT_EQ(bad->allocated_size(), prog2.allocated_original_size);
  EXPECT_EQ(bad->size(), prog2.symlink_size);
}

TEST_F(entry_test, root_dir_must_be_a_directory) {
  auto tree = writer::entry_storage{};

  EXPECT_THAT([&] { ef->create(tree, *os, sep / "somelink"); },
              testing::ThrowsMessage<dwarfs::runtime_error>(
                  testing::HasSubstr("must be a directory")));
}

TEST_F(entry_test, file_create_data_initializes_file_state) {
  auto tree = writer::entry_storage{};

  auto* root = ef->create(tree, *os, sep);
  ASSERT_TRUE(root);

  auto* empty = ef->create(tree, *os, sep / "empty", root);
  ASSERT_TRUE(empty);

  auto* f = empty->as_file();
  ASSERT_TRUE(f);

  f->create_data();

  EXPECT_FALSE(f->is_invalid());
  EXPECT_EQ(1, f->hardlink_count());
  EXPECT_FALSE(f->inode_num().has_value());
  EXPECT_TRUE(f->hash().empty());
}

TEST_F(entry_test, file_hardlink_shares_invalid_state) {
  auto tree = writer::entry_storage{};

  auto* root = ef->create(tree, *os, sep);
  ASSERT_TRUE(root);

  auto* foo_entry = ef->create(tree, *os, sep / "foo.pl", root);
  auto* bar_entry = ef->create(tree, *os, sep / "bar.pl", root);
  ASSERT_TRUE(foo_entry);
  ASSERT_TRUE(bar_entry);

  auto* foo = foo_entry->as_file();
  auto* bar = bar_entry->as_file();
  ASSERT_TRUE(foo);
  ASSERT_TRUE(bar);

  foo->create_data();

  writer::internal::progress prog{};
  bar->hardlink(foo, prog);

  EXPECT_FALSE(foo->is_invalid());
  EXPECT_FALSE(bar->is_invalid());

  foo->set_invalid();

  EXPECT_TRUE(foo->is_invalid());
  EXPECT_TRUE(bar->is_invalid());
}

TEST_F(entry_test, file_hardlink_shares_inode_num_state) {
  auto tree = writer::entry_storage{};

  auto* root = ef->create(tree, *os, sep);
  ASSERT_TRUE(root);

  auto* foo_entry = ef->create(tree, *os, sep / "foo.pl", root);
  auto* bar_entry = ef->create(tree, *os, sep / "bar.pl", root);
  ASSERT_TRUE(foo_entry);
  ASSERT_TRUE(bar_entry);

  auto* foo = foo_entry->as_file();
  auto* bar = bar_entry->as_file();
  ASSERT_TRUE(foo);
  ASSERT_TRUE(bar);

  foo->create_data();

  writer::internal::progress prog{};
  bar->hardlink(foo, prog);

  EXPECT_FALSE(foo->inode_num().has_value());
  EXPECT_FALSE(bar->inode_num().has_value());

  foo->set_inode_num(1234);

  ASSERT_TRUE(foo->inode_num().has_value());
  ASSERT_TRUE(bar->inode_num().has_value());
  EXPECT_EQ(1234, *foo->inode_num());
  EXPECT_EQ(1234, *bar->inode_num());
}

TEST_F(entry_test, file_hardlink_count_increments_and_is_shared) {
  auto tree = writer::entry_storage{};

  auto* root = ef->create(tree, *os, sep);
  ASSERT_TRUE(root);

  auto* foo_entry = ef->create(tree, *os, sep / "foo.pl", root);
  auto* bar_entry = ef->create(tree, *os, sep / "bar.pl", root);
  ASSERT_TRUE(foo_entry);
  ASSERT_TRUE(bar_entry);

  auto* foo = foo_entry->as_file();
  auto* bar = bar_entry->as_file();
  ASSERT_TRUE(foo);
  ASSERT_TRUE(bar);

  foo->create_data();

  EXPECT_EQ(1, foo->hardlink_count());

  writer::internal::progress prog{};
  auto const foo_size = foo->size();
  auto const foo_allocated_size = foo->allocated_size();

  bar->hardlink(foo, prog);

  EXPECT_EQ(2, foo->hardlink_count());
  EXPECT_EQ(2, bar->hardlink_count());

  EXPECT_EQ(foo_size, prog.hardlink_size);
  EXPECT_EQ(foo_allocated_size, prog.allocated_hardlink_size);
  EXPECT_EQ(1, prog.hardlinks);
}

TEST_F(entry_test, file_hardlink_shares_hash_state) {
  auto tree = writer::entry_storage{};

  auto* root = ef->create(tree, *os, sep);
  ASSERT_TRUE(root);

  auto* foo_entry = ef->create(tree, *os, sep / "foo.pl", root);
  auto* bar_entry = ef->create(tree, *os, sep / "bar.pl", root);
  ASSERT_TRUE(foo_entry);
  ASSERT_TRUE(bar_entry);

  auto* foo = foo_entry->as_file();
  auto* bar = bar_entry->as_file();
  ASSERT_TRUE(foo);
  ASSERT_TRUE(bar);

  foo->create_data();

  writer::internal::progress prog{};
  bar->hardlink(foo, prog);

  auto mm = os->open_file(sep / "foo.pl");
  foo->scan(mm, prog, "sha512-256");

  EXPECT_EQ(foo->hash(), bar->hash());
  EXPECT_FALSE(foo->hash().empty());
}

TEST_F(entry_test, file_inode_num_requires_data) {
  auto tree = writer::entry_storage{};

  auto* root = ef->create(tree, *os, sep);
  ASSERT_TRUE(root);

  auto* empty_entry = ef->create(tree, *os, sep / "empty", root);
  ASSERT_TRUE(empty_entry);

  auto* f = empty_entry->as_file();
  ASSERT_TRUE(f);

  ASSERT_DEATH(
      { [[maybe_unused]] auto const& ino = f->inode_num(); },
      "file data unset");
}

TEST_F(entry_test, file_set_inode_num_requires_data) {
  auto tree = writer::entry_storage{};

  auto* root = ef->create(tree, *os, sep);
  ASSERT_TRUE(root);

  auto* empty_entry = ef->create(tree, *os, sep / "empty", root);
  ASSERT_TRUE(empty_entry);

  auto* f = empty_entry->as_file();
  ASSERT_TRUE(f);

  ASSERT_DEATH({ f->set_inode_num(1); }, "file data unset");
}

TEST_F(entry_test, file_set_inode_num_only_once) {
  auto tree = writer::entry_storage{};

  auto* root = ef->create(tree, *os, sep);
  ASSERT_TRUE(root);

  auto* empty_entry = ef->create(tree, *os, sep / "empty", root);
  ASSERT_TRUE(empty_entry);

  auto* f = empty_entry->as_file();
  ASSERT_TRUE(f);

  f->create_data();
  f->set_inode_num(1234);

  ASSERT_DEATH(
      { f->set_inode_num(5678); },
      "attempt to set inode number more than once");
}

TEST_F(entry_test, file_set_inode_rejects_second_assignment) {
  auto tree = writer::entry_storage{};

  auto* root = ef->create(tree, *os, sep);
  ASSERT_TRUE(root);

  auto* empty_entry = ef->create(tree, *os, sep / "empty", root);
  ASSERT_TRUE(empty_entry);

  auto* f = empty_entry->as_file();
  ASSERT_TRUE(f);

  test::test_logger lgr;
  writer::internal::progress prog{};
  writer::internal::inode_manager im{lgr, prog, sep, {}, false};

  auto ino1 = im.create_inode();
  auto ino2 = im.create_inode();

  f->set_inode(ino1);

  ASSERT_DEATH({ f->set_inode(ino2); }, "inode already set for file");
}
