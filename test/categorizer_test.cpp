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

#include <gtest/gtest.h>

#include <fmt/format.h>

#include "dwarfs/filesystem_v2.h"
#include "dwarfs_tool_main.h"

#include "mmap_mock.h"
#include "test_helpers.h"
#include "test_logger.h"

using namespace dwarfs;

namespace fs = std::filesystem;
// namespace po = boost::program_options;

namespace {

auto test_dir = fs::path(TEST_DATA_DIR).make_preferred();
auto audio_data_dir = test_dir / "pcmaudio";

} // namespace

class categorizer_test : public testing::TestWithParam<std::string> {};

TEST_P(categorizer_test, end_to_end) {
  auto level = GetParam();

  auto input = std::make_shared<test::os_access_mock>();

  input->add("", {1, 040755, 1, 0, 0, 10, 42, 0, 0, 0});
  input->add_local_files(audio_data_dir);
  input->add_file("random", 4096, true);

  test::test_iolayer iolayer(input);

  std::vector<char const*> args{"mkdwarfs", "-i", "/",
                                "-o",       "-",  "--categorize"};

  auto exit_code = mkdwarfs_main(args, iolayer.get());

  EXPECT_EQ(exit_code, 0);

  auto fsimage = iolayer.out();

  auto mm = std::make_shared<test::mmap_mock>(std::move(fsimage));

  test::test_logger lgr;
  filesystem_v2 fs(lgr, mm);

  auto iv16 = fs.find("/test8.aiff");
  auto iv32 = fs.find("/test8.caf");

  EXPECT_TRUE(iv16);
  EXPECT_TRUE(iv32);
}

INSTANTIATE_TEST_SUITE_P(dwarfs, categorizer_test, ::testing::Values("debug"));
