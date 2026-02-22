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

#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include <dwarfs/thrift_lite/compact_reader.h>
#include <dwarfs/thrift_lite/compact_writer.h>

#include <dwarfs/gen-cpp-lite/thrift_lite_test.h> // generated

namespace {
namespace gen = dwarfs::thrift_lite::test;
namespace tl = dwarfs::thrift_lite;

auto serialize_compact(auto const& obj) -> std::vector<std::byte> {
  auto out = std::vector<std::byte>{};
  auto w = tl::compact_writer{out};
  obj.write(w);
  return out;
}

template <class T>
auto deserialize_compact(std::vector<std::byte> const& bytes) -> T {
  auto r = tl::compact_reader{bytes};
  auto obj = T{};
  obj.read(r);
  return obj;
}

} // namespace

TEST(generated, compat_v1_bytes_into_v2_skips_removed_field_42) {
  auto v1 = gen::CompatV1{};
  v1.a() = 1;
  v1.c() = 2;
  v1.removed_later().emplace(777); // field id 42 present in v1 bytes

  auto const bytes = serialize_compact(v1);
  auto const v2 = deserialize_compact<gen::CompatV2>(bytes);

  EXPECT_EQ(v2.a().value(), 1);
  EXPECT_EQ(v2.c().value(), 2);

  // New optional fields are absent.
  EXPECT_FALSE(v2.e().has_value());
  EXPECT_FALSE(v2.f().has_value());
}

TEST(generated, compat_v2_bytes_into_v1_skips_future_field_50) {
  auto v2 = gen::CompatV2{};
  v2.a() = 10;
  v2.c() = 20;

  // Write the future field with nested containers/structs.
  v2.future_field().emplace();
  auto& m = v2.future_field().value();
  m[1].push_back(gen::SmallStrings{});
  m[1].back().name() = "x";

  auto const bytes = serialize_compact(v2);
  auto const v1 = deserialize_compact<gen::CompatV1>(bytes);

  EXPECT_EQ(v1.a().value(), 10);
  EXPECT_EQ(v1.c().value(), 20);

  // Field 42 not present => not set.
  EXPECT_FALSE(v1.removed_later().has_value());
}
