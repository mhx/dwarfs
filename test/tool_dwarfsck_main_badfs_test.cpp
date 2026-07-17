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

#include <dwarfs/file_util.h>

#include "test_tool_main_tester.h"

using namespace dwarfs;
using namespace dwarfs::test;

namespace {

auto const testdata{std::filesystem::path{TEST_DATA_DIR} / "badfs"};

std::vector<std::string> find_all_filesystems() {
  std::vector<std::string> files;
  for (auto const& e : std::filesystem::directory_iterator(testdata)) {
    if (e.is_regular_file()) {
      files.push_back(e.path().filename().string());
    }
  }
  return files;
}

std::vector<std::string> const get_files() {
  static std::vector<std::string> files = find_all_filesystems();
  return files;
}

class dwarfsck_badfs : public ::testing::TestWithParam<std::string> {};

} // namespace

TEST_P(dwarfsck_badfs, test) {
  auto const filename = GetParam();
  auto const filepath = testdata / GetParam();

  auto t = dwarfsck_tester::create_with_image(read_file(filepath));
  EXPECT_NE(0, t.run({"image.dwarfs", "--check-integrity"})) << t.err();
  EXPECT_THAT(t.err(), ::testing::HasSubstr("error: "));
}

INSTANTIATE_TEST_SUITE_P(dwarfs, dwarfsck_badfs,
                         ::testing::ValuesIn(get_files()));
