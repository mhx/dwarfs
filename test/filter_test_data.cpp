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

#include "filter_test_data.h"

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
    // clang-format on
};

}

std::vector<filter_test_data> const& get_filter_tests() { return filter_tests; }

std::string PrintToString(filter_test_data const& data) {
  return data.test_name();
}

} // namespace dwarfs::test
