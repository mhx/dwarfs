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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <dwarfs/thrift_lite/enum_traits.h>
#include <dwarfs/thrift_lite/enum_utils.h>

#include <dwarfs/gen-cpp-lite/thrift_lite_test_types.h>

namespace {

namespace gen = dwarfs::thrift_lite::test;
namespace tl = dwarfs::thrift_lite;

} // namespace

TEST(thrift_lite, enum_traits_contains_all_members_in_decl_order) {
  using K = gen::RecordKind;
  using traits = tl::enum_traits<K>;

  auto const values = traits::values();
  auto const names = traits::names();

  ASSERT_EQ(values.size(), names.size());
  ASSERT_GE(values.size(), 3U);

  // Check a few representative entries (order matters).
  EXPECT_EQ(values[0], K::unknown);
  EXPECT_EQ(names[0], "unknown");

  EXPECT_EQ(values[1], K::file);
  EXPECT_EQ(names[1], "file");

  // Non-contiguous value present and named.
  auto found_reserved = false;
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (names[i] == "reserved_10") {
      found_reserved = true;
      EXPECT_EQ(static_cast<int>(values[i]), 10);
    }
  }
  EXPECT_TRUE(found_reserved);
}

TEST(thrift_lite, enum_utils_enum_to_string) {
  using K = gen::RecordKind;

  EXPECT_EQ(tl::enum_to_string(K::unknown).value(), "unknown");
  EXPECT_EQ(tl::enum_to_string(K::file).value(), "file");
  EXPECT_EQ(tl::enum_to_string(K::directory).value(), "directory");
  EXPECT_FALSE(tl::enum_to_string(static_cast<K>(111)).has_value());
}
