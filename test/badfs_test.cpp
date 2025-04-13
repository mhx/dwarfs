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
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <filesystem>
#include <sstream>

#include <gtest/gtest.h>

#include <dwarfs/logger.h>
#include <dwarfs/mmap.h>
#include <dwarfs/reader/filesystem_options.h>
#include <dwarfs/reader/filesystem_v2.h>

#include "test_helpers.h"
#include "test_logger.h"

using namespace dwarfs;

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

class bad_fs : public ::testing::TestWithParam<std::string> {};

} // namespace

TEST_P(bad_fs, test) {
  auto filename = testdata / GetParam();

  test::test_logger lgr;
  test::os_access_mock os;
  std::ostringstream oss;

  int nerror = 0;

  try {
    nerror = reader::filesystem_v2::identify(
        lgr, os, std::make_shared<mmap>(filename), oss, 9, 1, true,
        reader::filesystem_options::IMAGE_OFFSET_AUTO);
  } catch (std::exception const&) {
    nerror = 1;
  }

  EXPECT_GT(nerror, 0);
}

INSTANTIATE_TEST_SUITE_P(dwarfs, bad_fs, ::testing::ValuesIn(get_files()));
