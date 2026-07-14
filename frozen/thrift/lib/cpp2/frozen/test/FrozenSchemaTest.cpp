/*
 * SPDX-FileCopyrightText: Copyright (c) Meta Platforms, Inc. and affiliates.
 * SPDX-FileCopyrightText: Copyright (c) Marcus Holland-Moritz
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * This file is derived from fbthrift and has been modified by
 * Marcus Holland-Moritz for use in dwarfs.
 */

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <thrift/lib/cpp2/frozen/Frozen.h>
#include <thrift/lib/cpp2/frozen/HintTypes.h>

namespace apache::thrift::frozen {
namespace {

using SchemaValidationException = schema::SchemaValidationException;
using testing::HasSubstr;
using testing::ThrowsMessage;

schema::Layout makeLayout(int32_t size, int16_t bits) {
  schema::Layout layout;
  *layout.size() = size;
  *layout.bits() = bits;
  return layout;
}

schema::Field makeField(int16_t layoutId, int16_t offset) {
  schema::Field field;
  *field.layoutId() = layoutId;
  *field.offset() = offset;
  return field;
}

schema::MemorySchema toMemorySchema(schema::Schema schema) {
  schema::MemorySchema memorySchema;
  schema::convert(std::move(schema), memorySchema);
  return memorySchema;
}

template <typename T>
schema::Schema makeSchema(const T& value) {
  Layout<T> layout;
  LayoutRoot::layout(value, layout);

  schema::MemorySchema memorySchema;
  saveRoot(layout, memorySchema);

  schema::Schema schema;
  schema::convert(memorySchema, schema);
  return schema;
}

template <typename T>
void loadSchema(schema::Schema schema) {
  auto memorySchema = toMemorySchema(std::move(schema));
  Layout<T> layout;
  loadRoot(layout, memorySchema);
}

schema::Layout& rootLayout(schema::Schema& schema) {
  return schema.layouts()->at(*schema.rootLayout());
}

TEST(FrozenSchema, EmptyRootLayoutIsValid) {
  schema::MemorySchema schema;
  saveRoot(Layout<int>{}, schema);

  Layout<int> layout;
  EXPECT_NO_THROW(loadRoot(layout, schema));
  EXPECT_TRUE(layout.empty());
}

TEST(FrozenSchema, RejectsLayoutOverflowWhileSaving) {
  schema::MemorySchema schema;
  schema::MemorySchema::Helper helper(schema);

  constexpr auto kMaxLayoutId = std::numeric_limits<int16_t>::max();
  for (int32_t size = 0; size <= kMaxLayoutId; ++size) {
    schema::MemoryLayout layout;
    layout.setSize(size);
    helper.add(std::move(layout));
  }

  schema::MemoryLayout overflow;
  overflow.setSize(static_cast<int32_t>(kMaxLayoutId) + 1);
  EXPECT_THAT(
      [&] { helper.add(std::move(overflow)); },
      ThrowsMessage<std::runtime_error>(HasSubstr("Layout overflow")));
}

TEST(FrozenSchema, RejectsEmptyLayoutTable) {
  schema::MemorySchema schema;
  EXPECT_THAT(
      [&] { schema.validate(); },
      ThrowsMessage<SchemaValidationException>(
          HasSubstr("layout table is empty")));
}

TEST(FrozenSchema, RejectsTooManyLayoutsDuringConversion) {
  schema::Schema schema;
  constexpr auto kMinLayoutId = std::numeric_limits<int16_t>::min();
  for (int32_t id = kMinLayoutId; id <= 0; ++id) {
    schema.layouts()->emplace(static_cast<int16_t>(id), makeLayout(0, 0));
  }
  *schema.rootLayout() = 0;

  EXPECT_THAT(
      [&] { toMemorySchema(std::move(schema)); },
      ThrowsMessage<SchemaValidationException>(HasSubstr("too many layouts")));
}

TEST(FrozenSchema, RejectsSparseLayoutIdsDuringConversion) {
  schema::Schema schema;
  schema.layouts()->emplace(1, makeLayout(0, 0));
  *schema.rootLayout() = 1;

  EXPECT_THAT(
      [&] { toMemorySchema(std::move(schema)); },
      ThrowsMessage<SchemaValidationException>(
          HasSubstr("layout ids must form the dense range")));
}

TEST(FrozenSchema, RejectsInvalidRootLayoutId) {
  schema::Schema schema;
  schema.layouts()->emplace(0, makeLayout(0, 0));
  *schema.rootLayout() = -1;

  auto memorySchema = toMemorySchema(std::move(schema));
  EXPECT_THAT(
      [&] { memorySchema.validate(); },
      ThrowsMessage<SchemaValidationException>(
          HasSubstr("root layout id is out of range")));
}

TEST(FrozenSchema, RejectsNegativeLayoutSize) {
  schema::Schema schema;
  schema.layouts()->emplace(0, makeLayout(-1, 0));
  *schema.rootLayout() = 0;

  auto memorySchema = toMemorySchema(std::move(schema));
  EXPECT_THAT(
      [&] { memorySchema.validate(); },
      ThrowsMessage<SchemaValidationException>(
          HasSubstr("negative byte size")));
}

TEST(FrozenSchema, RejectsNegativeLayoutBits) {
  schema::Schema schema;
  schema.layouts()->emplace(0, makeLayout(0, -1));
  *schema.rootLayout() = 0;

  auto memorySchema = toMemorySchema(std::move(schema));
  EXPECT_THAT(
      [&] { memorySchema.validate(); },
      ThrowsMessage<SchemaValidationException>(HasSubstr("negative bit size")));
}

TEST(FrozenSchema, RejectsBitRegionLargerThanByteSize) {
  schema::Schema schema;
  schema.layouts()->emplace(0, makeLayout(1, 9));
  *schema.rootLayout() = 0;

  auto memorySchema = toMemorySchema(std::move(schema));
  EXPECT_THAT(
      [&] { memorySchema.validate(); },
      ThrowsMessage<SchemaValidationException>(
          HasSubstr("bit region does not fit in byte size")));
}

TEST(FrozenSchema, RejectsLoadedLayoutBitRegionLargerThanByteSize) {
  Layout<std::pair<float, float>> layout;
  layout.size = 1;
  layout.bits = 9;
  LoadRoot root;

  EXPECT_THAT(
      [&] { layout.validate(root); },
      ThrowsMessage<SchemaValidationException>(
          HasSubstr("frozen layout bit region does not fit in its byte size")));
}

TEST(FrozenSchema, RejectsOutOfRangeFieldLayoutId) {
  schema::Schema schema;
  auto root = makeLayout(1, 0);
  root.fields()->emplace(1, makeField(1, 0));
  schema.layouts()->emplace(0, std::move(root));
  *schema.rootLayout() = 0;

  auto memorySchema = toMemorySchema(std::move(schema));
  EXPECT_THAT(
      [&] { memorySchema.validate(); },
      ThrowsMessage<SchemaValidationException>(
          HasSubstr("field 1 references an out-of-range layout id")));
}

TEST(FrozenSchema, RejectsDuplicateFieldId) {
  schema::MemorySchema schema;
  schema::MemorySchema::Helper helper(schema);

  schema::MemoryLayout child;
  const auto childId = helper.add(std::move(child));

  schema::MemoryField first;
  first.setId(1);
  first.setLayoutId(childId);
  first.setOffset(0);

  auto second = first;

  schema::MemoryLayout root;
  root.addField(first);
  root.addField(second);
  schema.setRootLayoutId(helper.add(std::move(root)));

  EXPECT_THAT(
      [&] { schema.validate(); },
      ThrowsMessage<SchemaValidationException>(
          HasSubstr("duplicate field id 1")));
}

TEST(FrozenSchema, AllowsFieldReferencingEmptyLayout) {
  using Pair = std::pair<float, float>;
  auto schema = makeSchema(Pair{1.0f, 2.0f});
  const auto emptyLayoutId = static_cast<int16_t>(schema.layouts()->size());
  schema.layouts()->emplace(emptyLayoutId, makeLayout(0, 0));
  *rootLayout(schema).fields()->at(2).layoutId() = emptyLayoutId;

  EXPECT_NO_THROW(loadSchema<Pair>(std::move(schema)));
}

TEST(FrozenSchema, RejectsEmptyFieldOutsideParent) {
  using Pair = std::pair<float, float>;
  auto schema = makeSchema(Pair{1.0f, 2.0f});
  const auto emptyLayoutId = static_cast<int16_t>(schema.layouts()->size());
  schema.layouts()->emplace(emptyLayoutId, makeLayout(0, 0));
  auto& root = rootLayout(schema);
  *root.fields()->at(2).layoutId() = emptyLayoutId;
  *root.fields()->at(2).offset() = static_cast<int16_t>(*root.size() + 1);

  EXPECT_THAT(
      [&] { loadSchema<Pair>(std::move(schema)); },
      ThrowsMessage<SchemaValidationException>(HasSubstr(
          "empty field 'second' has a byte offset outside its "
          "parent layout")));
}

TEST(FrozenSchema, RejectsEmptyFieldBitOffsetOutsideParent) {
  using Pair = std::pair<int8_t, int8_t>;
  auto schema = makeSchema(Pair{1, 1});
  const auto emptyLayoutId = static_cast<int16_t>(schema.layouts()->size());
  schema.layouts()->emplace(emptyLayoutId, makeLayout(0, 0));
  auto& root = rootLayout(schema);
  *root.fields()->at(2).layoutId() = emptyLayoutId;
  *root.fields()->at(2).offset() =
      static_cast<int16_t>(-static_cast<int32_t>(*root.bits()) - 1);

  EXPECT_THAT(
      [&] { loadSchema<Pair>(std::move(schema)); },
      ThrowsMessage<SchemaValidationException>(HasSubstr(
          "empty field 'second' has a bit offset outside its "
          "parent layout")));
}

TEST(FrozenSchema, RejectsLayoutReferenceCycle) {
  schema::Schema schema;
  auto first = makeLayout(1, 0);
  auto second = makeLayout(1, 0);
  first.fields()->emplace(1, makeField(1, 0));
  second.fields()->emplace(1, makeField(0, 0));
  schema.layouts()->emplace(0, std::move(first));
  schema.layouts()->emplace(1, std::move(second));
  *schema.rootLayout() = 0;

  auto memorySchema = toMemorySchema(std::move(schema));
  EXPECT_THAT(
      [&] { memorySchema.validate(); },
      ThrowsMessage<SchemaValidationException>(
          HasSubstr("layout reference graph contains a cycle")));
}

TEST(FrozenSchema, EmptyBoolLayoutIsValid) {
  auto memorySchema = toMemorySchema(makeSchema(false));
  Layout<bool> layout;

  EXPECT_NO_THROW(loadRoot(layout, memorySchema));
  EXPECT_TRUE(layout.empty());
  EXPECT_FALSE(layout.view({nullptr, 0}));
}

TEST(FrozenSchema, RejectsInvalidBoolLayout) {
  auto schema = makeSchema(true);
  *rootLayout(schema).bits() = 2;
  *rootLayout(schema).size() = 1;

  EXPECT_THAT(
      [&] { loadSchema<bool>(std::move(schema)); },
      ThrowsMessage<SchemaValidationException>(
          HasSubstr("invalid packed bool layout")));
}

TEST(FrozenSchema, RejectsInvalidPackedIntegerLayout) {
  auto schema = makeSchema(int8_t{1});
  *rootLayout(schema).bits() = 9;
  *rootLayout(schema).size() = 2;

  EXPECT_THAT(
      [&] { loadSchema<int8_t>(std::move(schema)); },
      ThrowsMessage<SchemaValidationException>(
          HasSubstr("invalid packed integer layout")));
}

TEST(FrozenSchema, RejectsInvalidTrivialLayout) {
  auto schema = makeSchema(1.0f);
  *rootLayout(schema).size() = 3;

  EXPECT_THAT(
      [&] { loadSchema<float>(std::move(schema)); },
      ThrowsMessage<SchemaValidationException>(
          HasSubstr("invalid trivial layout")));
}

TEST(FrozenSchema, RejectsInvalidFixedSizeStringLayout) {
  using Fixed4 = FixedSizeString<4>;
  auto schema = makeSchema(Fixed4{"abcd"});
  *rootLayout(schema).size() = 3;

  EXPECT_THAT(
      [&] { loadSchema<Fixed4>(std::move(schema)); },
      ThrowsMessage<SchemaValidationException>(
          HasSubstr("invalid fixed-size string layout")));
}

TEST(FrozenSchema, RejectsBitFieldOutsideParent) {
  using Pair = std::pair<int8_t, int8_t>;
  auto schema = makeSchema(Pair{1, 1});
  auto& root = rootLayout(schema);
  *root.fields()->at(2).offset() = static_cast<int16_t>(-*root.bits());

  EXPECT_THAT(
      [&] { loadSchema<Pair>(std::move(schema)); },
      ThrowsMessage<SchemaValidationException>(
          HasSubstr("bit field 'second' extends beyond its parent layout")));
}

TEST(FrozenSchema, RejectsOverlappingBitFields) {
  using Pair = std::pair<int8_t, int8_t>;
  auto schema = makeSchema(Pair{1, 1});
  *rootLayout(schema).fields()->at(2).offset() = 0;

  EXPECT_THAT(
      [&] { loadSchema<Pair>(std::move(schema)); },
      ThrowsMessage<SchemaValidationException>(HasSubstr("overlap")));
}

TEST(FrozenSchema, RejectsByteFieldOutsideParent) {
  using Pair = std::pair<float, float>;
  auto schema = makeSchema(Pair{1.0f, 2.0f});
  *rootLayout(schema).fields()->at(2).offset() = 6;

  EXPECT_THAT(
      [&] { loadSchema<Pair>(std::move(schema)); },
      ThrowsMessage<SchemaValidationException>(
          HasSubstr("byte field 'second' extends beyond its parent layout")));
}

TEST(FrozenSchema, RejectsOverlappingByteFields) {
  using Pair = std::pair<float, float>;
  auto schema = makeSchema(Pair{1.0f, 2.0f});
  *rootLayout(schema).fields()->at(2).offset() = 0;

  EXPECT_THAT(
      [&] { loadSchema<Pair>(std::move(schema)); },
      ThrowsMessage<SchemaValidationException>(HasSubstr("overlap")));
}

TEST(FrozenSchema, RejectsByteFieldInPackedBitRegion) {
  using Pair = std::pair<int8_t, float>;
  auto schema = makeSchema(Pair{1, 2.0f});
  *rootLayout(schema).fields()->at(2).offset() = 0;

  EXPECT_THAT(
      [&] { loadSchema<Pair>(std::move(schema)); },
      ThrowsMessage<SchemaValidationException>(HasSubstr(
          "byte field 'second' overlaps its parent's packed-bit region")));
}

TEST(FrozenSchema, RejectsBitLayoutWithByteOffset) {
  using Pair = std::pair<int8_t, int8_t>;
  auto schema = makeSchema(Pair{1, 1});
  *rootLayout(schema).fields()->at(1).offset() = 1;

  EXPECT_THAT(
      [&] { loadSchema<Pair>(std::move(schema)); },
      ThrowsMessage<SchemaValidationException>(
          HasSubstr("bit field 'first' has a byte offset")));
}

TEST(FrozenSchema, RejectsByteLayoutWithBitOffset) {
  using Pair = std::pair<float, float>;
  auto schema = makeSchema(Pair{1.0f, 2.0f});
  *rootLayout(schema).fields()->at(1).offset() = -1;

  EXPECT_THAT(
      [&] { loadSchema<Pair>(std::move(schema)); },
      ThrowsMessage<SchemaValidationException>(
          HasSubstr("byte field 'first' has a bit offset")));
}

TEST(FrozenSchema, AllowsAbsentKnownField) {
  using Pair = std::pair<float, float>;
  auto schema = makeSchema(Pair{1.0f, 2.0f});
  rootLayout(schema).fields()->erase(2);

  EXPECT_NO_THROW(loadSchema<Pair>(std::move(schema)));
}

TEST(FrozenSchema, AllowsUnknownField) {
  using Pair = std::pair<float, float>;
  auto schema = makeSchema(Pair{1.0f, 2.0f});
  const auto firstLayoutId = *rootLayout(schema).fields()->at(1).layoutId();
  rootLayout(schema).fields()->emplace(123, makeField(firstLayoutId, 0));

  EXPECT_NO_THROW(loadSchema<Pair>(std::move(schema)));
}

TEST(FrozenSchema, ArrayItemLayoutIsOutOfLine) {
  auto schema = makeSchema(std::vector<int>{1, 2, 3});
  EXPECT_NO_THROW(loadSchema<std::vector<int>>(std::move(schema)));
}

TEST(FrozenSchema, DerivedRangeLayoutCombinesBaseAndDerivedFields) {
  using Map = std::unordered_map<int, int>;
  auto schema = makeSchema(Map{{1, 2}, {3, 4}});
  EXPECT_NO_THROW(loadSchema<Map>(std::move(schema)));
}

} // namespace
} // namespace apache::thrift::frozen
