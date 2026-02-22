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

#include <sstream>
#include <string>

#include <gtest/gtest.h>

#include <dwarfs/thrift_lite/debug_writer.h>

#include <dwarfs/gen-cpp-lite/thrift_lite_test.h> // generated

namespace {
namespace gen = dwarfs::thrift_lite::test;
namespace tl = dwarfs::thrift_lite;
} // namespace

TEST(generated, debug_output_snapshot_for_small_object) {
  auto s = gen::SmallStrings{};
  s.name() = "alice";
  s.comment() = "hi";
  s.tag().emplace("t");

  auto oss = std::ostringstream{};
  auto w = tl::debug_writer{oss};

  s.write(w);

  auto const got = oss.str();
  auto const expected = R"dbg(SmallStrings{
  1: name (string) = "alice",
  2: comment (string) = "hi",
  3: tag (string) = "t"
})dbg";

  EXPECT_EQ(expected, got);
}
