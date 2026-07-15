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
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <thrift/lib/cpp2/frozen/Frozen.h>
#include <thrift/lib/cpp2/frozen/HintTypes.h>
#include <thrift/lib/cpp2/frozen/test/gen-cpp-lite/Example_layouts.h>
#include <thrift/lib/cpp2/frozen/test/gen-cpp-lite/Example_types.h>

namespace apache::thrift::frozen {

struct WideStringForValidation : public std::vector<uint64_t> {
  using std::vector<uint64_t>::vector;
};

} // namespace apache::thrift::frozen

namespace apache::thrift {

template <>
struct IsString<frozen::WideStringForValidation> : std::true_type {};

} // namespace apache::thrift

namespace apache::thrift::frozen {
namespace {

using ::apache::thrift::test::Empty;
using ::apache::thrift::test::Person1;

using testing::AllOf;
using testing::AnyOf;
using testing::HasSubstr;
using testing::ThrowsMessage;

using PackedMap = std::map<uint32_t, uint64_t>;
using PackedMapItem = PackedMap::value_type;

template <typename View>
concept HasRawRange = requires(const View& view) { view.range(); };

static_assert(detail::is_blit_layout_v<double>);
static_assert(!detail::is_blit_layout_v<PackedMapItem>);
static_assert(!detail::is_blit_layout_v<detail::Block>);
static_assert(HasRawRange<Layout<std::vector<double>>::View>);
static_assert(!HasRawRange<Layout<std::vector<PackedMapItem>>::View>);

std::span<const byte> byteSpan(const std::string& data) {
  return {reinterpret_cast<const byte*>(data.data()), data.size()};
}

std::span<const byte> relocateWithMisalignedRangeData(
    const std::string& data,
    size_t distance,
    size_t alignment,
    std::vector<byte>& storage) {
  assert(alignment > 1);

  storage.resize(data.size() + alignment);
  const auto base = reinterpret_cast<uintptr_t>(storage.data());
  size_t shift = 0;
  while ((base + shift + distance) % alignment == 0) {
    ++shift;
  }
  std::memcpy(storage.data() + shift, data.data(), data.size());
  return {storage.data() + shift, data.size()};
}

template <typename T>
std::string freezeData(const T& value, Layout<T>& layout) {
  const auto size = LayoutRoot::layout(value, layout);
  std::string data(size, '\0');
  auto write =
      std::span<byte>(reinterpret_cast<byte*>(data.data()), data.size());
  ByteRangeFreezer::freeze(layout, value, write);
  data.resize(data.size() - write.size());
  return data;
}

template <typename T>
void expectValid(const T& value) {
  Layout<T> layout;
  const auto data = freezeData(value, layout);
  EXPECT_NO_THROW(validateFrozenData(layout, byteSpan(data)));
}

template <typename T, typename Layout>
auto readField(
    const std::string& data,
    DataValidationPosition parent,
    const Field<T, Layout>& field) {
  const auto byteOffset =
      parent.byteOffset + static_cast<size_t>(field.pos.offset);
  const auto bitOffset =
      parent.bitOffset + static_cast<size_t>(field.pos.bitOffset);
  return field.layout.view(
      {reinterpret_cast<const byte*>(data.data()) + byteOffset, bitOffset});
}

template <typename T, typename Layout, std::integral Value>
void writeField(
    std::string& data,
    DataValidationPosition parent,
    const Field<T, Layout>& field,
    Value value) {
  const auto bitOffset =
      (parent.byteOffset + static_cast<size_t>(field.pos.offset)) * 8 +
      parent.bitOffset + static_cast<size_t>(field.pos.bitOffset);
  const auto bits = field.layout.size != 0 ? sizeof(T) * 8 : field.layout.bits;
  dwarfs::bit_view(reinterpret_cast<byte*>(data.data()))
      .write({bitOffset, bits}, value);
}

template <typename Layout>
DataValidationPosition fieldPosition(
    DataValidationPosition parent, const Layout& field) {
  return {
      parent.byteOffset + static_cast<size_t>(field.pos.offset),
      parent.bitOffset + static_cast<size_t>(field.pos.bitOffset)};
}

template <typename Layout>
DataValidationPosition rangeDataPosition(
    const std::string& data,
    DataValidationPosition self,
    const Layout& layout) {
  return {self.byteOffset + readField(data, self, layout.distanceField), 0};
}

template <typename Layout>
DataValidationPosition rangeItemPosition(
    const std::string& data,
    DataValidationPosition self,
    const Layout& layout,
    size_t index) {
  const auto rangeData = rangeDataPosition(data, self, layout);
  if (layout.itemField.layout.size != 0) {
    return {rangeData.byteOffset + index * layout.itemField.layout.size, 0};
  }
  return {rangeData.byteOffset, index * layout.itemField.layout.bits};
}

template <typename T>
void expectValid(const T& value, ValidationOptions options) {
  Layout<T> layout;
  const auto data = freezeData(value, layout);
  EXPECT_NO_THROW(validateFrozenData(layout, byteSpan(data), options));
}

TEST(FrozenDataValidation, AcceptsValidFixedData) {
  expectValid(false);
  expectValid(true);
  expectValid(int32_t{12345});
  expectValid(1.25);
  expectValid(FixedSizeString<4>{"data"});
  expectValid(
      std::pair<int8_t, uint64_t>{1, std::numeric_limits<uint64_t>::max()});
  expectValid(
      std::optional<std::pair<int32_t, double>>{std::in_place, 42, 3.5});
  expectValid(detail::Block{.mask = 0x1234, .offset = 7});
}

TEST(FrozenDataValidation, AcceptsValidOutOfLineData) {
  expectValid(std::string{"frozen string"});
  expectValid(WideStringForValidation{1, 2, 3});
  expectValid(std::string{});
  expectValid(std::vector<int32_t>{1, 2, 3, 4});
  expectValid(
      std::vector<uint64_t>{0, 1, std::numeric_limits<uint64_t>::max()});
  expectValid(std::vector<double>{1.0, 2.0, 3.0});
  expectValid(
      std::vector<FixedSizeString<4>>{
          FixedSizeString<4>{"one!"}, FixedSizeString<4>{"two!"}});
  expectValid(std::vector<std::string>{"one", "", "three"});
  expectValid(std::vector<std::vector<int32_t>>{{1, 2}, {}, {3, 4, 5}});
  expectValid(
      std::pair<std::string, std::vector<std::string>>{
          "title", {"first", "second"}});
}

TEST(FrozenDataValidation, AcceptsValidAssociativeData) {
  const ValidationOptions options{.checkAssociativeConsistency = true};
  expectValid(std::map<uint32_t, uint64_t>{{1, 10}, {2, 20}, {3, 30}}, options);
  expectValid(
      std::unordered_map<uint32_t, uint64_t>{{1, 10}, {2, 20}, {3, 30}},
      options);
}

TEST(FrozenDataValidation, AssociativeConsistencyRejectsUnorderedKeys) {
  using Value = std::map<uint32_t, uint32_t>;
  auto layout = maximumLayout<Value>();
  auto data = freezeData(Value{{1, 10}, {2, 20}}, layout);

  const auto second = rangeItemPosition(data, {}, layout, 1);

  writeField(data, second, layout.itemField.layout.firstField, uint32_t{0});

  EXPECT_NO_THROW(validateFrozenData(layout, byteSpan(data)));
  EXPECT_THAT(
      [&] {
        validateFrozenData(
            layout,
            byteSpan(data),
            ValidationOptions{.checkAssociativeConsistency = true});
      },
      ThrowsMessage<DataValidationException>(AllOf(
          HasSubstr("[1]"),
          HasSubstr("ordered table keys are not strictly increasing"),
          HasSubstr("previous index=0"),
          HasSubstr("current index=1"),
          HasSubstr("expected previous key < current key"))));
}

TEST(FrozenDataValidation, AssociativeConsistencyRejectsDuplicateOrderedKeys) {
  using Value = std::map<uint32_t, uint32_t>;
  auto layout = maximumLayout<Value>();
  auto data = freezeData(Value{{1, 10}, {2, 20}}, layout);

  const auto second = rangeItemPosition(data, {}, layout, 1);

  writeField(data, second, layout.itemField.layout.firstField, uint32_t{1});

  EXPECT_NO_THROW(validateFrozenData(layout, byteSpan(data)));
  EXPECT_THAT(
      [&] {
        validateFrozenData(
            layout,
            byteSpan(data),
            ValidationOptions{.checkAssociativeConsistency = true});
      },
      ThrowsMessage<DataValidationException>(AllOf(
          HasSubstr("[1]"),
          HasSubstr("ordered table keys are not strictly increasing"))));
}

TEST(FrozenDataValidation, RejectsNonEmptyHashTableWithoutBuckets) {
  using Value = std::unordered_map<uint32_t, uint32_t>;
  auto layout = maximumLayout<Value>();
  auto data = freezeData(Value{{1, 10}, {2, 20}}, layout);

  const auto sparseTable = fieldPosition({}, layout.sparseTableField);

  writeField(
      data, sparseTable, layout.sparseTableField.layout.countField, size_t{0});

  EXPECT_THAT(
      [&] { validateFrozenData(layout, byteSpan(data)); },
      ThrowsMessage<DataValidationException>(AllOf(
          HasSubstr(".sparseTable"),
          HasSubstr("non-empty hash table has no buckets"),
          HasSubstr("item count=2"),
          HasSubstr("bucket count=0"))));
}

TEST(FrozenDataValidation, RejectsNonCanonicalHashBlockOffset) {
  using Value = std::unordered_map<uint32_t, uint32_t>;
  Value value;
  for (uint32_t i = 0; i < 30; ++i) {
    value.emplace(i, i + 1);
  }

  auto layout = maximumLayout<Value>();
  auto data = freezeData(value, layout);
  const auto sparseTable = fieldPosition({}, layout.sparseTableField);
  const auto block =
      rangeItemPosition(data, sparseTable, layout.sparseTableField.layout, 0);

  writeField(
      data,
      block,
      layout.sparseTableField.layout.itemField.layout.offsetField,
      uint64_t{1});

  EXPECT_THAT(
      [&] { validateFrozenData(layout, byteSpan(data)); },
      ThrowsMessage<DataValidationException>(AllOf(
          HasSubstr(".sparseTable[0].offset"),
          HasSubstr(
              "hash table block offset does not match preceding population"),
          HasSubstr("actual=1"),
          HasSubstr("expected=0"))));
}

TEST(FrozenDataValidation, RejectsHashBlockPopulationBeyondItems) {
  using Value = std::unordered_map<uint32_t, uint32_t>;
  auto layout = maximumLayout<Value>();
  auto data = freezeData(Value{{1, 10}, {2, 20}}, layout);
  const auto sparseTable = fieldPosition({}, layout.sparseTableField);
  const auto block =
      rangeItemPosition(data, sparseTable, layout.sparseTableField.layout, 0);

  writeField(
      data,
      block,
      layout.sparseTableField.layout.itemField.layout.maskField,
      std::numeric_limits<uint64_t>::max());

  EXPECT_THAT(
      [&] { validateFrozenData(layout, byteSpan(data)); },
      ThrowsMessage<DataValidationException>(AllOf(
          HasSubstr(".sparseTable[0].mask"),
          HasSubstr("hash table block population exceeds the item count"),
          HasSubstr("population=64"),
          HasSubstr("item count=2"))));
}

TEST(FrozenDataValidation, RejectsHashPopulationBelowItemCount) {
  using Value = std::unordered_map<uint32_t, uint32_t>;
  auto layout = maximumLayout<Value>();
  auto data = freezeData(Value{{1, 10}, {2, 20}}, layout);
  const auto sparseTable = fieldPosition({}, layout.sparseTableField);
  const auto block =
      rangeItemPosition(data, sparseTable, layout.sparseTableField.layout, 0);
  const auto& maskField =
      layout.sparseTableField.layout.itemField.layout.maskField;
  const auto mask = readField(data, block, maskField);
  ASSERT_EQ(2, std::popcount(mask));

  writeField(data, block, maskField, mask & (mask - 1));

  EXPECT_THAT(
      [&] { validateFrozenData(layout, byteSpan(data)); },
      ThrowsMessage<DataValidationException>(AllOf(
          HasSubstr(".sparseTable"),
          HasSubstr("hash table population does not match the item count"),
          HasSubstr("actual=1"),
          HasSubstr("expected=2"))));
}

TEST(FrozenDataValidation, AssociativeConsistencyRejectsInvalidHashIndex) {
  using Value = std::unordered_map<uint32_t, uint32_t>;
  auto layout = maximumLayout<Value>();
  auto data = freezeData(Value{{1, 10}, {2, 20}}, layout);

  const auto first = rangeItemPosition(data, {}, layout, 0);
  const auto second = rangeItemPosition(data, {}, layout, 1);
  const auto& keyField = layout.itemField.layout.firstField;
  const auto firstKey = readField(data, first, keyField);
  const auto secondKey = readField(data, second, keyField);
  ASSERT_NE(firstKey, secondKey);

  writeField(data, first, keyField, secondKey);
  writeField(data, second, keyField, firstKey);

  EXPECT_NO_THROW(validateFrozenData(layout, byteSpan(data)));
  EXPECT_THAT(
      [&] {
        validateFrozenData(
            layout,
            byteSpan(data),
            ValidationOptions{.checkAssociativeConsistency = true});
      },
      ThrowsMessage<DataValidationException>(AllOf(
          HasSubstr("[0]"),
          AnyOf(
              HasSubstr("hash table key is not reachable through its index"),
              HasSubstr("hash table key resolves to a different item")),
          HasSubstr("expected item index=0"))));
}

TEST(FrozenDataValidation, AssociativeConsistencyRejectsDuplicateHashKeys) {
  using Value = std::unordered_map<uint32_t, uint32_t>;
  auto layout = maximumLayout<Value>();
  auto data = freezeData(Value{{1, 10}, {2, 20}}, layout);

  const auto first = rangeItemPosition(data, {}, layout, 0);
  const auto second = rangeItemPosition(data, {}, layout, 1);
  const auto& keyField = layout.itemField.layout.firstField;
  const auto firstKey = readField(data, first, keyField);

  writeField(data, second, keyField, firstKey);

  EXPECT_NO_THROW(validateFrozenData(layout, byteSpan(data)));
  EXPECT_THAT(
      [&] {
        validateFrozenData(
            layout,
            byteSpan(data),
            ValidationOptions{.checkAssociativeConsistency = true});
      },
      ThrowsMessage<DataValidationException>(AllOf(
          HasSubstr("[1]"),
          HasSubstr("hash table key resolves to a different item"),
          HasSubstr("expected item index=1"))));
}

TEST(FrozenDataValidation, AcceptsRangeWithZeroStorageItems) {
  expectValid(std::vector<int32_t>(100, 0));
  expectValid(std::vector<std::pair<int32_t, int32_t>>(100));
}

TEST(FrozenDataValidation, AcceptsMisalignedPackedMapItems) {
  const PackedMap value{{1, 2}, {3, 4}, {5, 6}};
  Layout<PackedMap> layout;
  const auto data = freezeData(value, layout);
  const auto distance = readField(data, {}, layout.distanceField);

  std::vector<byte> storage;
  const auto relocated = relocateWithMisalignedRangeData(
      data, distance, alignof(PackedMapItem), storage);
  ASSERT_NE(
      0U,
      (reinterpret_cast<uintptr_t>(relocated.data()) + distance) %
          alignof(PackedMapItem));

  EXPECT_NO_THROW(validateFrozenData(layout, relocated));
  EXPECT_EQ(value, layout.view({relocated.data(), 0}).thaw());
}

TEST(FrozenDataValidation, RejectsMisalignedRelocatedBlitRange) {
  const std::vector<double> value{1.0, 2.0, 3.0};
  Layout<std::vector<double>> layout;
  const auto data = freezeData(value, layout);
  const auto distance = readField(data, {}, layout.distanceField);

  std::vector<byte> storage;
  const auto relocated =
      relocateWithMisalignedRangeData(data, distance, alignof(double), storage);

  EXPECT_THAT(
      [&] { validateFrozenData(layout, relocated); },
      ThrowsMessage<DataValidationException>(AllOf(
          HasSubstr("range data is not correctly aligned"),
          HasSubstr("expected alignment=" + std::to_string(alignof(double))))));
}

TEST(FrozenDataValidation, AcceptsGeneratedStructData) {
  Person1 person;
  *person.name() = "person";
  *person.height() = 1.75;
  person.age() = 42;
  auto& pet = person.pets()->emplace_back();
  *pet.name() = "pet";

  expectValid(person);
  expectValid(Empty{});
}

TEST(FrozenDataValidation, RejectsInvalidGeneratedStructFieldData) {
  Person1 person;
  *person.name() = "person";
  auto& pet = person.pets()->emplace_back();
  *pet.name() = "pet";

  auto layout = maximumLayout<Person1>();
  auto data = freezeData(person, layout);
  const auto pets = fieldPosition(DataValidationPosition{}, layout.petsField);
  const auto petsDistance =
      readField(data, pets, layout.petsField.layout.distanceField);
  const DataValidationPosition petPos{
      .byteOffset = pets.byteOffset + petsDistance};
  const auto name =
      fieldPosition(petPos, layout.petsField.layout.itemField.layout.nameField);

  writeField(
      data,
      name,
      layout.petsField.layout.itemField.layout.nameField.layout.distanceField,
      data.size() - LayoutRoot::kPaddingBytes);

  EXPECT_THAT(
      [&] { validateFrozenData(layout, byteSpan(data)); },
      ThrowsMessage<DataValidationException>(AllOf(
          HasSubstr(".pets[0].name"),
          HasSubstr("string data extends beyond the frozen data range"),
          HasSubstr("logical size="))));
}

TEST(FrozenDataValidation, RejectsStringDataOutsideFrozenRange) {
  auto layout = maximumLayout<std::string>();
  auto data = freezeData(std::string{"data"}, layout);

  writeField(
      data, {}, layout.distanceField, data.size() - LayoutRoot::kPaddingBytes);

  EXPECT_THAT(
      [&] { validateFrozenData(layout, byteSpan(data)); },
      ThrowsMessage<DataValidationException>(
          HasSubstr("string data extends beyond the frozen data range")));
}

TEST(FrozenDataValidation, RejectsStringDataOverlappingRoot) {
  auto layout = maximumLayout<std::string>();
  auto data = freezeData(std::string{"data"}, layout);

  writeField(data, {}, layout.distanceField, size_t{0});

  EXPECT_THAT(
      [&] { validateFrozenData(layout, byteSpan(data)); },
      ThrowsMessage<DataValidationException>(
          HasSubstr("string data overlaps root object")));
}

TEST(FrozenDataValidation, RejectsStringDataSizeOverflow) {
  auto layout = maximumLayout<WideStringForValidation>();
  auto data = freezeData(WideStringForValidation{1}, layout);

  writeField(data, {}, layout.countField, std::numeric_limits<size_t>::max());

  EXPECT_THAT(
      [&] { validateFrozenData(layout, byteSpan(data)); },
      ThrowsMessage<DataValidationException>(
          HasSubstr("string data overflows while computing its size")));
}

TEST(FrozenDataValidation, RejectsMisalignedStringData) {
  auto layout = maximumLayout<WideStringForValidation>();
  auto data = freezeData(WideStringForValidation{1}, layout);
  const auto distance = readField(data, {}, layout.distanceField);
  data.insert(data.end() - LayoutRoot::kPaddingBytes, '\0');

  writeField(data, {}, layout.distanceField, distance + 1);

  EXPECT_THAT(
      [&] { validateFrozenData(layout, byteSpan(data)); },
      ThrowsMessage<DataValidationException>(
          HasSubstr("string data is not correctly aligned")));
}

TEST(FrozenDataValidation, RejectsStringDataPositionOverflow) {
  using Value = std::vector<std::string>;
  auto layout = maximumLayout<Value>();
  auto data = freezeData(Value{"data"}, layout);
  const auto itemOffset = readField(data, {}, layout.distanceField);
  const DataValidationPosition item{.byteOffset = itemOffset};

  writeField(
      data,
      item,
      layout.itemField.layout.distanceField,
      std::numeric_limits<size_t>::max());

  EXPECT_THAT(
      [&] { validateFrozenData(layout, byteSpan(data)); },
      ThrowsMessage<DataValidationException>(
          HasSubstr("string data position overflows while adding offsets")));
}

TEST(FrozenDataValidation, RejectsRangeDataOutsideFrozenRange) {
  auto layout = maximumLayout<std::vector<int32_t>>();
  auto data = freezeData(std::vector<int32_t>{1, 2}, layout);

  writeField(
      data, {}, layout.distanceField, data.size() - LayoutRoot::kPaddingBytes);

  EXPECT_THAT(
      [&] { validateFrozenData(layout, byteSpan(data)); },
      ThrowsMessage<DataValidationException>(
          HasSubstr("range data extends beyond the frozen data range")));
}

TEST(FrozenDataValidation, RejectsRangeDataOverlappingRoot) {
  auto layout = maximumLayout<std::vector<int32_t>>();
  auto data = freezeData(std::vector<int32_t>{1, 2}, layout);

  writeField(data, {}, layout.distanceField, size_t{0});

  EXPECT_THAT(
      [&] { validateFrozenData(layout, byteSpan(data)); },
      ThrowsMessage<DataValidationException>(
          HasSubstr("range data overlaps root object")));
}

TEST(FrozenDataValidation, RejectsZeroStorageRangePositionOutsideData) {
  using Value = std::vector<int32_t>;
  Value value(4, 0);
  Layout<Value> layout;
  layout.distanceField.layout.bits = sizeof(size_t) * 8;
  layout.distanceField.layout.inlined = true;
  auto data = freezeData(value, layout);

  ASSERT_TRUE(layout.itemField.layout.empty());

  writeField(data, {}, layout.distanceField, data.size() + 1);

  EXPECT_THAT(
      [&] { validateFrozenData(layout, byteSpan(data)); },
      ThrowsMessage<DataValidationException>(HasSubstr(
          "range data position performs a read beyond the frozen data range")));
}

TEST(FrozenDataValidation, RejectsRangeDataPositionOverflow) {
  using Value = std::vector<std::vector<int32_t>>;
  auto layout = maximumLayout<Value>();
  auto data = freezeData(Value{{1, 2}}, layout);
  const auto itemOffset = readField(data, {}, layout.distanceField);
  const DataValidationPosition item{.byteOffset = itemOffset};

  writeField(
      data,
      item,
      layout.itemField.layout.distanceField,
      std::numeric_limits<size_t>::max());

  EXPECT_THAT(
      [&] { validateFrozenData(layout, byteSpan(data)); },
      ThrowsMessage<DataValidationException>(
          HasSubstr("range data position overflows while adding offsets")));
}

TEST(FrozenDataValidation, RejectsRangeDataSizeOverflow) {
  auto layout = maximumLayout<std::vector<double>>();
  auto data = freezeData(std::vector<double>{1.0}, layout);

  writeField(data, {}, layout.countField, std::numeric_limits<size_t>::max());

  EXPECT_THAT(
      [&] { validateFrozenData(layout, byteSpan(data)); },
      ThrowsMessage<DataValidationException>(
          HasSubstr("range data overflows while computing its size")));
}

TEST(FrozenDataValidation, RejectsMisalignedRangeData) {
  auto layout = maximumLayout<std::vector<double>>();
  auto data = freezeData(std::vector<double>{1.0}, layout);
  const auto distance = readField(data, {}, layout.distanceField);
  data.insert(data.end() - LayoutRoot::kPaddingBytes, '\0');

  writeField(data, {}, layout.distanceField, distance + 1);

  EXPECT_THAT(
      [&] { validateFrozenData(layout, byteSpan(data)); },
      ThrowsMessage<DataValidationException>(AllOf(
          HasSubstr("range data is not correctly aligned"),
          HasSubstr("actual remainder="),
          HasSubstr("expected alignment=" + std::to_string(alignof(double))))));
}

TEST(FrozenDataValidation, RejectsOverlappingStringPayloads) {
  using Value = std::pair<std::string, std::string>;
  auto layout = maximumLayout<Value>();
  auto data = freezeData(Value{"first", "second"}, layout);

  const auto first = fieldPosition(DataValidationPosition{}, layout.firstField);
  const auto second =
      fieldPosition(DataValidationPosition{}, layout.secondField);
  const auto firstDistance =
      readField(data, first, layout.firstField.layout.distanceField);
  const auto firstDataOffset = first.byteOffset + firstDistance;
  ASSERT_GE(firstDataOffset, second.byteOffset);

  writeField(
      data,
      second,
      layout.secondField.layout.distanceField,
      firstDataOffset - second.byteOffset);

  EXPECT_THAT(
      [&] { validateFrozenData(layout, byteSpan(data)); },
      ThrowsMessage<DataValidationException>(AllOf(
          HasSubstr(".second"),
          HasSubstr("string data overlaps string data"),
          HasSubstr("actual interval=["),
          HasSubstr("conflicting location="),
          HasSubstr(".first"))));
}

TEST(FrozenDataValidation, RejectsInvalidNestedStringPayload) {
  using Value = std::vector<std::string>;
  auto layout = maximumLayout<Value>();
  auto data = freezeData(Value{"nested"}, layout);

  const auto rangeDistance = readField(data, {}, layout.distanceField);
  const DataValidationPosition item{.byteOffset = rangeDistance};

  writeField(
      data,
      item,
      layout.itemField.layout.distanceField,
      data.size() - LayoutRoot::kPaddingBytes);

  EXPECT_THAT(
      [&] { validateFrozenData(layout, byteSpan(data)); },
      ThrowsMessage<DataValidationException>(
          HasSubstr("string data extends beyond the frozen data range")));
}

TEST(FrozenDataValidation, AcceptsEmptyRoot) {
  Layout<int32_t> layout;
  std::array<byte, LayoutRoot::kPaddingBytes> data{};

  EXPECT_NO_THROW(validateFrozenData(layout, data));
}

TEST(FrozenDataValidation, RejectsMissingPackedReadPadding) {
  Layout<int32_t> layout;
  std::array<byte, LayoutRoot::kPaddingBytes - 1> data{};

  EXPECT_THAT(
      [&] { validateFrozenData(layout, data); },
      ThrowsMessage<DataValidationException>(
          HasSubstr("missing the trailing packed-read padding")));
}

TEST(FrozenDataValidation, RejectsRootOutsideLogicalData) {
  Layout<float> layout;
  auto data = freezeData(1.0f, layout);

  ASSERT_GT(data.size(), LayoutRoot::kPaddingBytes);
  data.erase(data.end() - LayoutRoot::kPaddingBytes - 1);

  EXPECT_THAT(
      [&] { validateFrozenData(layout, byteSpan(data)); },
      ThrowsMessage<DataValidationException>(
          HasSubstr("root object extends beyond the frozen data range")));
}

TEST(FrozenDataValidation, RejectsByteLayoutWithDataBitOffset) {
  detail::Block value{.mask = 0x1234, .offset = 7};
  Layout<detail::Block> layout;
  const auto data = freezeData(value, layout);

  // mask is a byte layout. Corrupt its loaded position so BlockLayout's
  // recursive validation reaches LayoutBase::validateData() with a bit offset.
  layout.maskField.pos = FieldPosition{0, 1};

  EXPECT_THAT(
      [&] { validateFrozenData(layout, byteSpan(data)); },
      ThrowsMessage<DataValidationException>(AllOf(
          HasSubstr(".mask"),
          HasSubstr("byte layout has a non-zero data bit offset"),
          HasSubstr("actual=1"),
          HasSubstr("expected=0"))));
}

TEST(FrozenDataValidation, RetainsValidationOptions) {
  std::array<byte, 2 * LayoutRoot::kPaddingBytes> data{};
  DataValidationContext context(
      data, ValidationOptions{.checkAssociativeConsistency = true});

  EXPECT_TRUE(context.options().checkAssociativeConsistency);
  EXPECT_EQ(LayoutRoot::kPaddingBytes, context.logicalSize());
}

TEST(FrozenDataValidation, RejectsPositionOverflow) {
  std::array<byte, LayoutRoot::kPaddingBytes> data{};
  DataValidationContext context(data);

  EXPECT_THAT(
      [&] {
        context.position(
            {std::numeric_limits<size_t>::max(), 0}, FieldPosition{1, 0});
      },
      ThrowsMessage<DataValidationException>(
          HasSubstr("field position overflows while adding offsets")));
  EXPECT_THAT(
      [&] {
        context.position(
            {0, std::numeric_limits<size_t>::max()}, FieldPosition{0, 1});
      },
      ThrowsMessage<DataValidationException>(
          HasSubstr("field bit position overflows while adding offsets")));
}

TEST(FrozenDataValidation, RejectsInvalidLoadedFieldPosition) {
  std::array<byte, LayoutRoot::kPaddingBytes> data{};
  DataValidationContext context(data);

  EXPECT_THAT(
      [&] { context.position({}, FieldPosition{-1, 0}); },
      ThrowsMessage<DataValidationException>(
          HasSubstr("invalid loaded field position")));
  EXPECT_THAT(
      [&] { context.position({}, FieldPosition{0, -1}); },
      ThrowsMessage<DataValidationException>(
          HasSubstr("invalid loaded field position")));
}

TEST(FrozenDataValidation, RejectsPhysicalReadOutsideData) {
  std::array<byte, LayoutRoot::kPaddingBytes> data{};
  DataValidationContext context(data);

  EXPECT_THAT(
      [&] {
        context.requirePhysicalBytes(data.size(), 1, "test physical read");
      },
      ThrowsMessage<DataValidationException>(HasSubstr(
          "test physical read performs a read beyond the frozen data range")));
}

TEST(FrozenDataValidation, AcceptsZeroBitPackedRead) {
  std::array<byte, LayoutRoot::kPaddingBytes> data{};
  DataValidationContext context(data);

  // Zero-bit layouts perform no read, so neither the position nor word size
  // needs to be inspected.
  EXPECT_NO_THROW(context.requirePackedRead(
      {std::numeric_limits<size_t>::max(), std::numeric_limits<size_t>::max()},
      0,
      3,
      "zero-bit packed read"));
}

TEST(FrozenDataValidation, RejectsInvalidPackedReadWordSize) {
  std::array<byte, LayoutRoot::kPaddingBytes> data{};
  DataValidationContext context(data);

  EXPECT_THAT(
      [&] { context.requirePackedRead({}, 1, 3, "test packed read"); },
      ThrowsMessage<DataValidationException>(AllOf(
          HasSubstr("invalid packed-read word size"),
          HasSubstr("actual=3"),
          HasSubstr("expected a non-zero power of two"))));
}

TEST(FrozenDataValidation, RejectsInvalidAlignmentRequirement) {
  std::array<byte, LayoutRoot::kPaddingBytes> data{};
  DataValidationContext context(data);

  EXPECT_THAT(
      [&] { context.requireAlignment(0, 3, "test allocation"); },
      ThrowsMessage<DataValidationException>(AllOf(
          HasSubstr("invalid frozen-data alignment"),
          HasSubstr("actual=3"),
          HasSubstr("expected a non-zero power of two"))));
}

TEST(FrozenDataValidation, RejectsSizeOverflow) {
  std::array<byte, LayoutRoot::kPaddingBytes> data{};
  DataValidationContext context(data);

  EXPECT_THAT(
      [&] {
        context.checkedMultiply(
            std::numeric_limits<size_t>::max(), 2, "test allocation");
      },
      ThrowsMessage<DataValidationException>(
          HasSubstr("test allocation overflows while computing its size")));
}

TEST(FrozenDataValidation, RejectsOverlappingAllocations) {
  std::array<byte, 24> data{};
  DataValidationContext context(data);

  context.registerAllocation(0, 8, "first allocation");
  EXPECT_THAT(
      [&] { context.registerAllocation(4, 8, "second allocation"); },
      ThrowsMessage<DataValidationException>(
          HasSubstr("second allocation overlaps first allocation")));
}

TEST(FrozenDataValidation, RejectsAllocationOverlappingFollowingAllocation) {
  std::array<byte, 24> data{};
  DataValidationContext context(data);

  context.registerAllocation(8, 8, "following allocation");
  EXPECT_THAT(
      [&] { context.registerAllocation(4, 8, "preceding allocation"); },
      ThrowsMessage<DataValidationException>(
          HasSubstr("preceding allocation overlaps following allocation")));
}

TEST(FrozenDataValidation, AcceptsAdjacentAndEmptyAllocations) {
  std::array<byte, 24> data{};
  DataValidationContext context(data);

  context.registerAllocation(0, 8, "first allocation");
  EXPECT_NO_THROW(context.registerAllocation(8, 8, "second allocation"));
  EXPECT_NO_THROW(context.registerAllocation(0, 0, "empty allocation"));
}

TEST(FrozenDataValidation, RejectsMisalignedData) {
  std::array<byte, 24> data{};
  DataValidationContext context(std::span<const byte>(data).subspan(1));

  const auto base = reinterpret_cast<uintptr_t>(data.data() + 1);
  size_t offset = 0;
  while (((base + offset) & 7U) == 0) {
    ++offset;
  }
  EXPECT_THAT(
      [&] { context.requireAlignment(offset, 8, "test allocation"); },
      ThrowsMessage<DataValidationException>(
          HasSubstr("test allocation is not correctly aligned")));
}

} // namespace
} // namespace apache::thrift::frozen
