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

#include <filesystem>
#include <sstream>

#include <gtest/gtest.h>

#include "dwarfs/filesystem_v2.h"
#include "dwarfs/logger.h"
#include "dwarfs/mmap.h"
#include "dwarfs/options.h"

#include "test_logger.h"

using namespace dwarfs;

namespace {

const auto testdata{std::filesystem::path{TEST_DATA_DIR} / "badfs"};

std::vector<std::string> files;

void find_all_filesystems() {
  for (auto const& e : std::filesystem::directory_iterator(testdata)) {
    if (e.is_regular_file()) {
      files.push_back(e.path().filename().string());
    }
  }
}

class bad_fs : public ::testing::TestWithParam<std::string> {};

} // namespace

TEST_P(bad_fs, test) {
  auto filename = testdata / GetParam();

  test::test_logger lgr;
  std::ostringstream oss;

  int nerror = 0;

  try {
    nerror =
        filesystem_v2::identify(lgr, std::make_shared<mmap>(filename), oss, 9,
                                1, true, filesystem_options::IMAGE_OFFSET_AUTO);
  } catch (std::exception const&) {
    nerror = 1;
  }

  EXPECT_GT(nerror, 0);
}

INSTANTIATE_TEST_SUITE_P(dwarfs, bad_fs, ::testing::ValuesIn(files));

int main(int argc, char** argv) {
  find_all_filesystems();
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
