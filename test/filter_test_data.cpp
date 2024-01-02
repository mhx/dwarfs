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

#include <ostream>

#include <fmt/format.h>

#include "filter_test_data.h"
#include "test_helpers.h"

namespace dwarfs::test {

namespace {

std::vector<dwarfs::test::filter_test_data> const filter_tests{
    // clang-format off
{
  "IncludeAllSharedObjs",
R"(
+ *.so
- *
)", {
  "",
  "usr",
  "usr/lib",
  "usr/lib/python3.10",
  "usr/lib/python3.10/lib-dynload",
  "usr/lib/python3.10/lib-dynload/_asyncio.cpython-310-x86_64-linux-gnu.so",
  "usr/lib/python3.10/lib-dynload/audioop.cpython-310-x86_64-linux-gnu.so",
  "usr/lib/python3.10/lib-dynload/_codecs_tw.cpython-310-x86_64-linux-gnu.so",
  "usr/lib/python3.10/lib-dynload/_elementtree.cpython-310-x86_64-linux-gnu.so",
  "usr/lib/gcc",
  "usr/lib/gcc/x86_64-pc-linux-gnu",
  "usr/lib/gcc/x86_64-pc-linux-gnu/11.3.0",
  "usr/lib/gcc/x86_64-pc-linux-gnu/11.3.0/libitm.so",
  "usr/lib/gcc/x86_64-pc-linux-gnu/11.3.0/32",
  "usr/lib/gcc/x86_64-pc-linux-gnu/11.3.0/32/libatomic.so",
  "usr/lib64",
  "usr/lib64/xtables",
  "usr/lib64/xtables/libxt_state.so",
  "usr/lib64/xtables/libxt_LED.so",
  "usr/lib64/xtables/libxt_policy.so",
  "usr/lib64/xtables/libxt_udp.so",
  "usr/lib64/gconv",
  "usr/lib64/gconv/IBM500.so",
  "usr/lib64/gconv/libCNS.so",
  "usr/lib64/gconv/ISO8859-16.so",
  "lib",
  "lib/libpcprofile.so",
}},
{
  "IncludeSomeObjects",
R"(
- gcc/**.o
+ *.o
- *
)", {
  "",
  "usr",
  "usr/lib",
  "usr/lib/Mcrt1.o",
  "usr/lib64",
  "usr/lib64/gcrt1.o",
}},
    // clang-format on
};

}

std::vector<filter_test_data> const& get_filter_tests() { return filter_tests; }

std::ostream& operator<<(std::ostream& os, filter_test_data const& data) {
  os << data.test_name();
  return os;
}

std::string
filter_test_data::get_expected_filter_output(debug_filter_mode mode) const {
  std::string expected;

  auto check_included = [&](auto const& stat, std::string const& path,
                            std::string_view prefix = "") {
    if (stat.type() == posix_file_type::directory) {
      expected += fmt::format("{}/{}/\n", prefix, path);
    } else if (expected_files().count(path)) {
      expected += fmt::format("{}/{}\n", prefix, path);
    }
  };

  auto check_included_files = [&](auto const& stat, std::string const& path,
                                  std::string_view prefix = "") {
    if (stat.type() != posix_file_type::directory &&
        expected_files().count(path)) {
      expected += fmt::format("{}/{}\n", prefix, path);
    }
  };

  auto check_excluded = [&](auto const& stat, std::string const& path,
                            std::string_view prefix = "") {
    if (stat.type() != posix_file_type::directory &&
        expected_files().count(path) == 0) {
      expected += fmt::format("{}/{}\n", prefix, path);
    }
  };

  auto check_excluded_files = [&](auto const& stat, std::string const& path,
                                  std::string_view prefix = "") {
    if (stat.type() != posix_file_type::directory &&
        expected_files().count(path) == 0) {
      expected += fmt::format("{}/{}\n", prefix, path);
    }
  };

  for (auto const& [stat, name] : dwarfs::test::test_dirtree()) {
    std::string path(name.substr(name.size() == 5 ? 5 : 6));

    if (path.empty()) {
      continue;
    }

    switch (mode) {
    case debug_filter_mode::INCLUDED_FILES:
      check_included_files(stat, path);
      break;

    case debug_filter_mode::INCLUDED:
      check_included(stat, path);
      break;

    case debug_filter_mode::EXCLUDED_FILES:
      check_excluded_files(stat, path);
      break;

    case debug_filter_mode::EXCLUDED:
      check_excluded(stat, path);
      break;

    case debug_filter_mode::FILES:
      check_included_files(stat, path, "+ ");
      check_excluded_files(stat, path, "- ");
      break;

    case debug_filter_mode::ALL:
      check_included(stat, path, "+ ");
      check_excluded(stat, path, "- ");
      break;

    case debug_filter_mode::OFF:
      throw std::logic_error("invalid debug filter mode");
    }
  }

  return expected;
}
} // namespace dwarfs::test
