/*
 * SPDX-FileCopyrightText: Copyright (c) Meta Platforms, Inc. and affiliates.
 * SPDX-FileCopyrightText: Copyright (c) Marcus Holland-Moritz
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * This file is derived from fbthrift and has been modified by
 * Marcus Holland-Moritz for use in dwarfs.
 */

#include <array>
#include <memory>
#include <span>
#include <string>

#include <gtest/gtest.h>

#include <thrift/lib/cpp2/frozen/FrozenUtil.h>
#include <thrift/lib/cpp2/frozen/HintTypes.h>
#include <thrift/lib/cpp2/frozen/test/gen-cpp-lite/Example_layouts.h>
#include <thrift/lib/cpp2/frozen/test/gen-cpp-lite/Example_types.h>

namespace apache::thrift::frozen {

TEST(FrozenVectorTypes, Unpacked) {
  VectorUnpacked<int> viu{2, 3, 5, 7, 9, 11, 13, 17};
  std::vector<int> vip = viu;
  EXPECT_LT(frozenSize(vip), frozenSize(viu));
  auto fiu = freeze(viu);
  EXPECT_EQ(fiu[2], 5);
  EXPECT_EQ(fiu.end()[-1], 17);
  const int* raw = fiu.data();
  EXPECT_EQ(raw[3], 7);
}

TEST(FrozenFixedSizeString, RoundTrip) {
  FixedSizeString<4> s{"abcd"};
  auto mapped = mapFrozen<FixedSizeString<4>>(freezeToString(s));
  EXPECT_EQ(4, mapped.size());
  EXPECT_EQ("abcd", mapped.toString());
}

TEST(FrozenFixedSizeString, FreezeSizeMismatchThrows) {
  EXPECT_THROW(
      freezeToString(FixedSizeString<4>{"toolong"}),
      detail::FixedSizeMismatchException);
}

TEST(FrozenFixedSizeString, EmptyLayoutViewAndThaw) {
  // A default-constructed layout is exactly the state produced by loading a
  // schema in which the field is absent (Field::load is never called, e.g.
  // for files written by an older version of a struct). view() used to
  // ignore the layout and unconditionally read kFixedSize bytes at the view
  // position.
  Layout<FixedSizeString<4>> layout;
  ASSERT_TRUE(layout.empty());

  // Allocate a single byte on the heap so that ASAN catches out-of-bounds
  // reads should they be reintroduced.
  auto buffer = std::make_unique<uint8_t>(0xAA);
  ViewPosition pos{buffer.get(), 0};

  auto view = layout.view(pos);
  EXPECT_TRUE(view.empty());
  EXPECT_EQ("", view.toString());

  FixedSizeString<4> out{"junk"};
  layout.thaw(pos, out);
  EXPECT_TRUE(out.empty());
}

TEST(FrozenFixedSizeString, ViewEquality) {
  using View = Layout<FixedSizeString<4>>::View;
  std::array<uint8_t, 4> abcd{'a', 'b', 'c', 'd'};
  std::array<uint8_t, 4> abcx{'a', 'b', 'c', 'x'};
  View va{std::span<uint8_t const>{abcd}};
  View vb{std::span<uint8_t const>{abcx}};
  View e1{};
  View e2{};
  EXPECT_TRUE(va == View{std::span<uint8_t const>{abcd}});
  EXPECT_FALSE(va == vb);
  EXPECT_TRUE(e1 == e2);
  EXPECT_FALSE(va == e1);
  EXPECT_FALSE(e1 == va);
}

namespace {

// Emulates reading a file written by an older version of a struct in which
// the field `id` did not yet exist, by stripping the field from the
// serialized schema before loading it.
void stripFieldFromRootLayout(schema::MemorySchema& memSchema, int16_t id) {
  schema::Schema schema;
  convert(memSchema, schema);
  schema.layouts()->at(*schema.rootLayout()).fields()->erase(id);
  convert(std::move(schema), memSchema);
}

} // namespace

TEST(FrozenFixedSizeString, AbsentFieldReadsAsDefault) {
  test::TestFixedSizeString x;
  *x.bytes8() = std::string{"abcdefgh"};

  Layout<test::TestFixedSizeString> layout;
  LayoutRoot::layout(x, layout);
  auto data = freezeDataToString(x, layout);

  schema::MemorySchema memSchema;
  saveRoot(layout, memSchema);
  stripFieldFromRootLayout(memSchema, 1); // 1: bytes8

  Layout<test::TestFixedSizeString> stripped;
  loadRoot(stripped, memSchema);

  auto view = stripped.view({reinterpret_cast<const byte*>(data.data()), 0});
  // The field is absent from the schema, so it must read as the default
  // value, even though the static type says kFixedSize == 8.
  EXPECT_TRUE(view.bytes8().empty());
  EXPECT_EQ("", view.bytes8().toString());
}
} // namespace apache::thrift::frozen
