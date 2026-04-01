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

#include <gtest/gtest.h>

#include <dwarfs/writer/entry_factory.h>

#include <dwarfs/writer/internal/entry.h>
#include <dwarfs/writer/internal/progress.h>

#include "test_helpers.h"

using namespace dwarfs;
namespace fs = std::filesystem;

namespace {

using wi_entry = writer::internal::entry;
using wi_dir = writer::internal::dir;
using wi_file = writer::internal::file;
using wi_link = writer::internal::link;
using wi_device = writer::internal::device;
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

  void visit(wi_file* p) override {
    events.push_back("file:" + p->unix_dpath());
  }
  void visit(wi_device* p) override {
    events.push_back("device:" + p->unix_dpath());
  }
  void visit(wi_link* p) override {
    events.push_back("link:" + p->unix_dpath());
  }
  void visit(wi_dir* p) override { events.push_back("dir:" + p->unix_dpath()); }
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
  auto e1 = ef->create(*os, sep);
  auto e2 = ef->create(*os, sep / "somelink", e1);
  auto e3 = ef->create(*os, sep / "somedir", e1);
  auto e4 = ef->create(*os, sep / "somedir" / "ipsum.py", e3);

  EXPECT_FALSE(e1->has_parent());
  EXPECT_TRUE(e1->is_directory());
  EXPECT_EQ(e1->type(), wi_entry::E_DIR);

  EXPECT_EQ(sep.string(), e1->name());
  EXPECT_EQ(sep, e1->fs_path());
  EXPECT_EQ(sep.string(), e1->path_as_string());
  EXPECT_EQ("/", e1->unix_dpath());

  EXPECT_TRUE(e2->has_parent());
  EXPECT_FALSE(e2->is_directory());
  EXPECT_EQ(e2->type(), wi_entry::E_LINK);

  EXPECT_EQ("somelink", e2->name());
  EXPECT_EQ(sep / "somelink", e2->fs_path());
  EXPECT_EQ((sep / "somelink").string(), e2->path_as_string());
  EXPECT_EQ("/somelink", e2->unix_dpath());

  EXPECT_TRUE(e3->has_parent());
  EXPECT_TRUE(e3->is_directory());
  EXPECT_EQ(e3->type(), wi_entry::E_DIR);

  EXPECT_EQ("somedir", e3->name());
  EXPECT_EQ(sep / "somedir", e3->fs_path());
  EXPECT_EQ((sep / "somedir").string(), e3->path_as_string());
  EXPECT_EQ("/somedir/", e3->unix_dpath());

  EXPECT_TRUE(e4->has_parent());
  EXPECT_FALSE(e4->is_directory());
  EXPECT_EQ(e4->type(), wi_entry::E_FILE);

  EXPECT_EQ("ipsum.py", e4->name());
  EXPECT_EQ(sep / "somedir" / "ipsum.py", e4->fs_path());
  EXPECT_EQ((sep / "somedir" / "ipsum.py").string(), e4->path_as_string());
  EXPECT_EQ("/somedir/ipsum.py", e4->unix_dpath());
}

TEST_F(entry_test, factory_creates_expected_entry_kinds) {
  struct test_case {
    fs::path path;
    wi_entry::type_t type;
    bool is_directory;
  };

  std::vector<test_case> cases{
      {sep, wi_entry::E_DIR, true},
      {sep / "test.pl", wi_entry::E_FILE, false},
      {sep / "somelink", wi_entry::E_LINK, false},
      {sep / "somedir", wi_entry::E_DIR, true},
      {sep / "somedir" / "ipsum.py", wi_entry::E_FILE, false},
      {sep / "somedir" / "null", wi_entry::E_DEVICE, false},
      {sep / "somedir" / "zero", wi_entry::E_DEVICE, false},
      {sep / "somedir" / "pipe", wi_entry::E_OTHER, false},
  };

  for (auto const& tc : cases) {
    auto e = ef->create(*os, tc.path);
    ASSERT_TRUE(e) << tc.path;
    EXPECT_EQ(tc.type, e->type()) << tc.path;
    EXPECT_EQ(tc.is_directory, e->is_directory()) << tc.path;
  }
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

  auto root = local_ef.create(*local_os, root_path);
  auto a = local_ef.create(*local_os, root_path / "a", root);
  auto b = local_ef.create(*local_os, root_path / "b", root);
  auto ax = local_ef.create(*local_os, root_path / "a" / "x", a);
  auto bx = local_ef.create(*local_os, root_path / "b" / "x", b);

  ASSERT_TRUE(root);
  ASSERT_TRUE(a);
  ASSERT_TRUE(b);
  ASSERT_TRUE(ax);
  ASSERT_TRUE(bx);

  EXPECT_FALSE(root->has_parent());
  EXPECT_TRUE(a->has_parent());
  EXPECT_TRUE(ax->has_parent());

  EXPECT_EQ(root.get(), a->parent().get());
  EXPECT_EQ(a.get(), ax->parent().get());
  ASSERT_TRUE(ax->parent()->parent());
  EXPECT_EQ(root.get(), ax->parent()->parent().get());

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

  auto root =
      std::dynamic_pointer_cast<wi_dir>(local_ef.create(*local_os, root_path));
  auto a = std::dynamic_pointer_cast<wi_dir>(
      local_ef.create(*local_os, root_path / "a", root));
  auto b = local_ef.create(*local_os, root_path / "b", root);
  auto c = local_ef.create(*local_os, root_path / "a" / "c", a);

  ASSERT_TRUE(root);
  ASSERT_TRUE(a);
  ASSERT_TRUE(b);
  ASSERT_TRUE(c);

  root->add(a);
  root->add(b);
  a->add(c);

  std::vector<std::string> visited;
  root->walk([&](wi_entry* e) { visited.push_back(e->unix_dpath()); });

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

  auto root =
      std::dynamic_pointer_cast<wi_dir>(local_ef.create(*local_os, root_path));
  auto a = std::dynamic_pointer_cast<wi_dir>(
      local_ef.create(*local_os, root_path / "a", root));
  auto b = local_ef.create(*local_os, root_path / "b", root);
  auto c = local_ef.create(*local_os, root_path / "a" / "c", a);

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
  // Existing test fixture root is below the threshold.
  auto root = std::dynamic_pointer_cast<wi_dir>(ef->create(*os, sep));
  ASSERT_TRUE(root);

  auto somelink = ef->create(*os, sep / "somelink", root);
  auto somedir = ef->create(*os, sep / "somedir", root);
  auto test_pl = ef->create(*os, sep / "test.pl", root);

  root->add(somelink);
  root->add(somedir);
  root->add(test_pl);

  auto found_small = root->find("somelink");
  ASSERT_TRUE(found_small);
  EXPECT_EQ("somelink", found_small->name());
  EXPECT_EQ(wi_entry::E_LINK, found_small->type());
  EXPECT_EQ(nullptr, root->find("does-not-exist"));

  // Synthetic large directory to force the lookup-table path.
  auto local_os = std::make_shared<test::os_access_mock>();
  writer::entry_factory local_ef;
  auto root_path = make_root_path();

  local_os->add_dir(root_path);
  for (int i = 0; i < 20; ++i) {
    local_os->add_file(root_path / ("f" + std::to_string(i)), "x");
  }

  auto big_root =
      std::dynamic_pointer_cast<wi_dir>(local_ef.create(*local_os, root_path));
  ASSERT_TRUE(big_root);

  for (int i = 0; i < 20; ++i) {
    big_root->add(local_ef.create(
        *local_os, root_path / ("f" + std::to_string(i)), big_root));
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

  auto root =
      std::dynamic_pointer_cast<wi_dir>(local_ef.create(*local_os, root_path));
  auto keep = std::dynamic_pointer_cast<wi_dir>(
      local_ef.create(*local_os, root_path / "keep", root));
  auto keep_file =
      local_ef.create(*local_os, root_path / "keep" / "file.txt", keep);
  auto empty_a = std::dynamic_pointer_cast<wi_dir>(
      local_ef.create(*local_os, root_path / "empty_a", root));
  auto nested = std::dynamic_pointer_cast<wi_dir>(
      local_ef.create(*local_os, root_path / "nested", root));
  auto empty_b = std::dynamic_pointer_cast<wi_dir>(
      local_ef.create(*local_os, root_path / "nested" / "empty_b", nested));

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
  root->walk([&](wi_entry* e) { before.push_back(e->unix_dpath()); });
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
  root->walk([&](wi_entry* e) { after.push_back(e->unix_dpath()); });
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
  auto root = ef->create(*os, sep);
  ASSERT_TRUE(root);

  auto somelink = std::dynamic_pointer_cast<wi_link>(
      ef->create(*os, sep / "somelink", root));
  auto somedir =
      std::dynamic_pointer_cast<wi_dir>(ef->create(*os, sep / "somedir", root));
  auto bad = std::dynamic_pointer_cast<wi_link>(
      ef->create(*os, sep / "somedir" / "bad", somedir));

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
