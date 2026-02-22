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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <dwarfs/thrift_lite/compact_reader.h>
#include <dwarfs/thrift_lite/compact_writer.h>

#include <dwarfs/thrift_lite/internal/compact_wire.h>

#include <dwarfs/gen-cpp-lite/thrift_lite_test_types.h>

namespace {

namespace gen = dwarfs::thrift_lite::test;
namespace tl = dwarfs::thrift_lite;

using testing::ElementsAre;
using testing::Pair;
using testing::UnorderedElementsAre;

auto serialize_compact(auto const& obj) -> std::vector<std::byte> {
  auto out = std::vector<std::byte>{};
  auto w = tl::compact_writer{out};
  obj.write(w);
  return out;
}

template <typename T>
auto deserialize_compact(std::vector<std::byte> const& bytes) -> T {
  auto r = tl::compact_reader{bytes};
  auto obj = T{};
  obj.read(r);
  return obj;
}

} // namespace

TEST(thrift_lite, compat_v1_bytes_into_v2_skips_removed_field_42) {
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

TEST(thrift_lite, compat_v2_bytes_into_v1_skips_future_field_50) {
  auto v2 = gen::CompatV2{};
  v2.a() = 10;
  v2.c() = 20;

  // Write the future field with nested containers/structs.
  v2.future_field().emplace();
  auto& m = v2.future_field().value();
  m[1].emplace_back();
  m[1].back().name() = "x";

  auto const bytes = serialize_compact(v2);
  auto const v1 = deserialize_compact<gen::CompatV1>(bytes);

  EXPECT_EQ(v1.a().value(), 10);
  EXPECT_EQ(v1.c().value(), 20);

  // Field 42 not present => not set.
  EXPECT_FALSE(v1.removed_later().has_value());
}

TEST(thrift_lite, empty_optional_list_is_cleared) {
  auto in = gen::Containers{};
  in.opt_list().emplace(); // empty, but present
  auto const bytes = serialize_compact(in);

  auto out = gen::Containers{};
  out.opt_list().emplace();
  out.opt_list()->emplace_back(42);

  EXPECT_EQ(1, out.opt_list()->size());

  auto r = tl::compact_reader{bytes};
  out.read(r);

  ASSERT_TRUE(out.opt_list().has_value());
  EXPECT_TRUE(out.opt_list()->empty());
}

TEST(thrift_lite, empty_optional_set_is_cleared) {
  auto in = gen::Containers{};
  in.opt_set().emplace(); // empty, but present
  auto const bytes = serialize_compact(in);

  auto out = gen::Containers{};
  out.opt_set().emplace();
  out.opt_set()->emplace(42);

  EXPECT_EQ(1, out.opt_set()->size());

  auto r = tl::compact_reader{bytes};
  out.read(r);

  ASSERT_TRUE(out.opt_set().has_value());
  EXPECT_TRUE(out.opt_set()->empty());
}

TEST(thrift_lite, empty_optional_map_is_cleared) {
  auto in = gen::Containers{};
  in.opt_map().emplace(); // empty, but present
  auto const bytes = serialize_compact(in);

  auto out = gen::Containers{};
  out.opt_map().emplace();
  out.opt_map()->emplace(42, "foo");

  EXPECT_EQ(1, out.opt_map()->size());

  auto r = tl::compact_reader{bytes};
  out.read(r);

  ASSERT_TRUE(out.opt_map().has_value());
  EXPECT_TRUE(out.opt_map()->empty());
}

TEST(thrift_lite, handle_mismatched_types_gracefully) {
  auto in = gen::MismatchedV1{};

  in.a_bool() = true;
  in.a_list()->emplace_back(42);
  in.a_set()->emplace("foo");
  in.a_map()->emplace(1, "bar");
  in.opt_bool().emplace(true);
  in.opt_list().emplace();
  in.opt_list()->emplace_back(43);
  in.opt_set().emplace();
  in.opt_set()->emplace("hello");
  in.opt_map().emplace();
  in.opt_map()->emplace(2, "world");
  in.unchanged() = 123;

  auto bytes = serialize_compact(in);

  {
    auto out = deserialize_compact<gen::MismatchedV2>(bytes);

    EXPECT_TRUE(out.a_string()->empty());
    EXPECT_TRUE(out.a_list()->empty());
    EXPECT_TRUE(out.a_set()->empty());
    EXPECT_TRUE(out.a_map()->empty());
    EXPECT_FALSE(out.opt_string().has_value());
    EXPECT_TRUE(out.opt_list().has_value());
    EXPECT_TRUE(out.opt_list()->empty());
    EXPECT_TRUE(out.opt_set().has_value());
    EXPECT_TRUE(out.opt_set()->empty());
    EXPECT_TRUE(out.opt_map().has_value());
    EXPECT_TRUE(out.opt_map()->empty());
    ASSERT_TRUE(out.unchanged().has_value());
    EXPECT_EQ(123, out.unchanged().value());
  }

  {
    auto out = deserialize_compact<gen::MismatchedV1>(bytes);

    EXPECT_TRUE(out.a_bool().value());
    EXPECT_THAT(out.a_list().value(), ElementsAre(42));
    EXPECT_THAT(out.a_set().value(), UnorderedElementsAre("foo"));
    EXPECT_THAT(out.a_map().value(), UnorderedElementsAre(Pair(1, "bar")));
    EXPECT_TRUE(out.opt_bool().has_value());
    EXPECT_TRUE(out.opt_bool().value());
    EXPECT_TRUE(out.opt_list().has_value());
    EXPECT_THAT(out.opt_list().value(), ElementsAre(43));
    EXPECT_TRUE(out.opt_set().has_value());
    EXPECT_THAT(out.opt_set().value(), UnorderedElementsAre("hello"));
    EXPECT_TRUE(out.opt_map().has_value());
    EXPECT_THAT(out.opt_map().value(), UnorderedElementsAre(Pair(2, "world")));
    EXPECT_EQ(123, out.unchanged().value());
  }
}

TEST(thrift_lite, compact_type_id_for_field_panic) {
  EXPECT_EQ(tl::internal::ct_double,
            tl::internal::compact_type_id_for_field(tl::ttype::double_t));
  EXPECT_EQ(tl::internal::ct_uuid,
            tl::internal::compact_type_id_for_field(tl::ttype::uuid_t));
  EXPECT_EQ(tl::internal::ct_stop,
            tl::internal::compact_type_id_for_field(tl::ttype::stop_t));
  EXPECT_DEATH(static_cast<void>(
                   tl::internal::compact_type_id_for_field(tl::ttype::bool_t)),
               "bool_t must be encoded via field-header TRUE/FALSE or element "
               "bool encoding");
  EXPECT_DEATH(static_cast<void>(tl::internal::compact_type_id_for_field(
                   static_cast<tl::ttype>(1000))),
               "unknown ttype");
}
