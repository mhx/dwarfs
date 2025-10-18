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

#include <array>
#include <filesystem>
#include <sstream>

#include <gtest/gtest.h>

#include <dwarfs/logger.h>
#include <dwarfs/reader/filesystem_options.h>
#include <dwarfs/reader/filesystem_v2.h>

#include "test_helpers.h"
#include "test_logger.h"

using namespace dwarfs;

namespace {

using namespace std::string_view_literals;

constexpr std::array kSkipOn32Bit = {
    "0161bfabd70ee4d3700a46dbe0bf2335.dwarfs"sv,
    "0ca44aa3dda67fe9e9dd85bafbcf8c65.dwarfs"sv,
    "1ee0685c6ec60cc83d204dcd2a86cf6e.dwarfs"sv,
    "288e74070d7e82ba12a6c7f87c7b74c2.dwarfs"sv,
    "320da6d7bce5948ef356e4fe01b20275.dwarfs"sv,
    "38528a6800d8907065e9bc3de6545030.dwarfs"sv,
    "3935bf683501ba8e0812b96a32f9e9c1.dwarfs"sv,
    "3cdd36c5bfdcad8f1cb11f3757b10e0d.dwarfs"sv,
    "67eb016e1ec15aef9e50ddac8119544f.dwarfs"sv,
    "72028fdf38bc8bf5767467a8eb33cea1.dwarfs"sv,
    "80c6ae30d257cf7a936eafa54c85e0f4.dwarfs"sv,
    "af9384d3fac4850ed2f10125b5db730c.dwarfs"sv,
    "b5c4dfdbba53dda0eea180ae3acccebc.dwarfs"sv,
    "ccbfc9eb10aa7b89138996ab90a172a1.dwarfs"sv,
    "f93cd8ed5de226bca0ecefc521df9f13.dwarfs"sv,
};

#ifdef DWARFS_TEST_RUNNING_ON_ASAN
constexpr std::array kSkipWithAsan = {
    "02064956b00513713fde656f9738fc17.dwarfs"sv,
    "29351be64bffd8bd07f8f1943c8869fd.dwarfs"sv,
    "2e68f4eb874ea525200d2566c2265af6.dwarfs"sv,
    "2f6193322fe8ca159229be308ed71399.dwarfs"sv,
    "35a475fba1c80cb40a9816240e935044.dwarfs"sv,
    "83f03c7abac9eda814d41496bf2ab149.dwarfs"sv,
    "910764780f74966a91d1120c7bfc67b4.dwarfs"sv,
    "abb59522034feda17a598a3464704294.dwarfs"sv,
    "bc90491054b1a3ba11296d73ad763667.dwarfs"sv,
    "d1d617c7f2d86dcadf2c757b0fdc6133.dwarfs"sv,
    "d4f117ce06b45c4594a2e17b03db75cc.dwarfs"sv,
    "e9afadd7d4935680fff771aded537e33.dwarfs"sv,
};
#endif

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
  auto const filename = GetParam();
  auto const filepath = testdata / GetParam();

  if (sizeof(size_t) == 4 && std::find(kSkipOn32Bit.begin(), kSkipOn32Bit.end(),
                                       filename) != kSkipOn32Bit.end()) {
    GTEST_SKIP() << "skipping test for 32-bit systems: " << filename;
  }

#ifdef DWARFS_TEST_RUNNING_ON_ASAN
  if (std::ranges::find(kSkipWithAsan, filename) != kSkipWithAsan.end()) {
    GTEST_SKIP() << "skipping test for ASAN builds: " << filename;
  }
#endif

  test::test_logger lgr;
  test::os_access_mock os;
  std::ostringstream oss;

  int nerror = 0;

  try {
    nerror = reader::filesystem_v2::identify(
        lgr, os, test::make_real_file_view(filepath), oss, 9, 1, true,
        reader::filesystem_options::IMAGE_OFFSET_AUTO);
  } catch (std::exception const&) {
    nerror = 1;
  }

  EXPECT_GT(nerror, 0);
}

INSTANTIATE_TEST_SUITE_P(dwarfs, bad_fs, ::testing::ValuesIn(get_files()));
