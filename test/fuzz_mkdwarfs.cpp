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

#include <folly/FileUtil.h>

#include "dwarfs_tool_main.h"
#include "test_helpers.h"

using namespace dwarfs;

int main(int argc, char** argv) {
  if (argc < 2) {
    return 1;
  }

  std::shared_ptr<test::os_access_mock> os{
      test::os_access_mock::create_test_instance()};

#ifdef __AFL_LOOP
  while (__AFL_LOOP(10000))
#endif
  {
    std::string cmdline;
    if (!folly::readFile(argv[1], cmdline)) {
      std::terminate();
    }

    std::shared_ptr<test::test_file_access> fa{
        std::make_shared<test::test_file_access>()};
    std::unique_ptr<test::test_iolayer> iol{
        std::make_unique<test::test_iolayer>(os, fa)};

    auto args = test::parse_args(cmdline);
    args.insert(args.begin(), "mkdwarfs");

    try {
      mkdwarfs_main(args, iol->get());
    } catch (const std::exception&) {
    }
  }

  return 0;
}
