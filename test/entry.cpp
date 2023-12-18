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

#include <gtest/gtest.h>

#include "dwarfs/entry.h"

#include "test_helpers.h"

using namespace dwarfs;
namespace fs = std::filesystem;

struct entry_test : public ::testing::Test {
  fs::path sep{
#ifdef _WIN32
      std::wstring
#else
      std::string
#endif
      (1, fs::path::preferred_separator)};
  std::shared_ptr<test::os_access_mock> os;
  std::unique_ptr<entry_factory> ef;

  void SetUp() override {
    os = test::os_access_mock::create_test_instance();
    ef = entry_factory::create();
  }

  void TearDown() override {
    ef.reset();
    os.reset();
  }
};

TEST_F(entry_test, path) {
  auto e1 = ef->create(*os, sep);
  auto e2 = ef->create(*os, fs::path("somelink"), e1);
  auto e3 = ef->create(*os, fs::path("somedir"), e1);
  auto e4 = ef->create(*os, fs::path("somedir") / "ipsum.py", e3);

  EXPECT_FALSE(e1->has_parent());
  EXPECT_TRUE(e1->is_directory());
  EXPECT_EQ(e1->type(), entry::E_DIR);

  EXPECT_EQ(sep.string(), e1->name());
  EXPECT_EQ(sep, e1->fs_path());
  EXPECT_EQ(sep.string(), e1->path_as_string());
  EXPECT_EQ(sep.string(), e1->dpath());
  EXPECT_EQ("/", e1->unix_dpath());

  EXPECT_TRUE(e2->has_parent());
  EXPECT_FALSE(e2->is_directory());
  EXPECT_EQ(e2->type(), entry::E_LINK);

  EXPECT_EQ("somelink", e2->name());
  EXPECT_EQ(sep / "somelink", e2->fs_path());
  EXPECT_EQ((sep / "somelink").string(), e2->path_as_string());
  EXPECT_EQ((sep / "somelink").string(), e2->dpath());
  EXPECT_EQ("/somelink", e2->unix_dpath());

  EXPECT_TRUE(e3->has_parent());
  EXPECT_TRUE(e3->is_directory());
  EXPECT_EQ(e3->type(), entry::E_DIR);

  EXPECT_EQ("somedir", e3->name());
  EXPECT_EQ(sep / "somedir", e3->fs_path());
  EXPECT_EQ((sep / "somedir").string(), e3->path_as_string());
  EXPECT_EQ((sep / "somedir").string() + sep.string(), e3->dpath());
  EXPECT_EQ("/somedir/", e3->unix_dpath());

  EXPECT_TRUE(e4->has_parent());
  EXPECT_FALSE(e4->is_directory());
  EXPECT_EQ(e4->type(), entry::E_FILE);

  EXPECT_EQ("ipsum.py", e4->name());
  EXPECT_EQ(sep / "somedir" / "ipsum.py", e4->fs_path());
  EXPECT_EQ((sep / "somedir" / "ipsum.py").string(), e4->path_as_string());
  EXPECT_EQ((sep / "somedir" / "ipsum.py").string(), e4->dpath());
  EXPECT_EQ("/somedir/ipsum.py", e4->unix_dpath());
}
