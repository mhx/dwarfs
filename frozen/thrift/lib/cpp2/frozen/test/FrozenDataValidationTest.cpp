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

struct WideStringForInspection : public std::vector<uint64_t> {
  using std::vector<uint64_t>::vector;
};

struct FixedInspectionProbe {
  uint32_t value;
};

struct DynamicInspectionProbe {
  uint32_t value;
};

template <>
struct Layout<FixedInspectionProbe>
    : detail::TrivialLayout<FixedInspectionProbe> {
  using Base = detail::TrivialLayout<FixedInspectionProbe>;

  static inline size_t inspectionCount = 0;

  void inspectData(
      DataInspectionContext& context, DataPosition position) const override {
    ++inspectionCount;
    Base::inspectData(context, position);
  }
};

template <>
struct Layout<DynamicInspectionProbe>
    : detail::TrivialLayout<DynamicInspectionProbe> {
  using Base = detail::TrivialLayout<DynamicInspectionProbe>;

  static constexpr bool kMayRequirePerItemInspection = true;
  static inline size_t inspectionCount = 0;

  void inspectData(
      DataInspectionContext& context, DataPosition position) const override {
    ++inspectionCount;
    Base::inspectData(context, position);
  }
};

} // namespace apache::thrift::frozen

namespace apache::thrift {

template <>
struct IsString<frozen::WideStringForInspection> : std::true_type {};

} // namespace apache::thrift

namespace apache::thrift::frozen {
namespace {

using ::apache::thrift::test::Empty;
using ::apache::thrift::test::Nesting;
using ::apache::thrift::test::Person1;
using ::apache::thrift::test::Ratio;

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

static_assert(!detail::may_require_per_item_inspection_v<Layout<bool>>);
static_assert(!detail::may_require_per_item_inspection_v<Layout<int32_t>>);
static_assert(!detail::may_require_per_item_inspection_v<Layout<double>>);
static_assert(
    !detail::may_require_per_item_inspection_v<Layout<FixedSizeString<4>>>);
static_assert(
    !detail::may_require_per_item_inspection_v<Layout<detail::Block>>);
static_assert(
    !detail::may_require_per_item_inspection_v<Layout<std::optional<int32_t>>>);
static_assert(detail::may_require_per_item_inspection_v<
              Layout<std::optional<std::string>>>);
static_assert(!detail::may_require_per_item_inspection_v<
              Layout<std::pair<int32_t, double>>>);
static_assert(detail::may_require_per_item_inspection_v<
              Layout<std::pair<int32_t, std::string>>>);
static_assert(
    detail::may_require_per_item_inspection_v<Layout<std::vector<int32_t>>>);
static_assert(!detail::may_require_per_item_inspection_v<Layout<Ratio>>);
static_assert(!detail::may_require_per_item_inspection_v<Layout<Nesting>>);
static_assert(detail::may_require_per_item_inspection_v<Layout<Person1>>);
static_assert(!detail::may_require_per_item_inspection_v<Layout<Empty>>);

std::span<const byte> byteSpan(const std::string& data) {
  return {reinterpret_cast<const byte*>(data.data()), data.size()};
}

void registerTestRegion(
    DataInspectionContext& context,
    const LayoutBase& layout,
    size_t offset,
    size_t size,
    std::string_view what) {
  context.registerRegion(
      DataRegion{
          .layout = &layout,
          .kind = DataRegionKind::RootObject,
          .offset = offset,
          .size = size,
      },
      what);
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
  EXPECT_NO_THROW(inspectFrozenData(layout, byteSpan(data)));
}

template <typename T, typename Layout>
auto readField(
    const std::string& data,
    DataPosition parent,
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
    DataPosition parent,
    const Field<T, Layout>& field,
    Value value) {
  const auto bitOffset =
      (parent.byteOffset + static_cast<size_t>(field.pos.offset)) * 8 +
      parent.bitOffset + static_cast<size_t>(field.pos.bitOffset);
  const auto bits = field.layout.size != 0 ? sizeof(T) * 8 : field.layout.bits;
  dwarfs::container::bit_view(reinterpret_cast<byte*>(data.data()))
      .write({bitOffset, bits}, value);
}

template <typename Layout>
DataPosition fieldPosition(DataPosition parent, const Layout& field) {
  return {
      parent.byteOffset + static_cast<size_t>(field.pos.offset),
      parent.bitOffset + static_cast<size_t>(field.pos.bitOffset)};
}

template <typename Layout>
DataPosition rangeDataPosition(
    const std::string& data, DataPosition self, const Layout& layout) {
  return {self.byteOffset + readField(data, self, layout.distanceField), 0};
}

template <typename Layout>
DataPosition rangeItemPosition(
    const std::string& data,
    DataPosition self,
    const Layout& layout,
    size_t index) {
  const auto rangeData = rangeDataPosition(data, self, layout);
  if (layout.itemField.layout.size != 0) {
    return {rangeData.byteOffset + index * layout.itemField.layout.size, 0};
  }
  return {rangeData.byteOffset, index * layout.itemField.layout.bits};
}

template <typename T>
void expectValid(const T& value, DataInspectionOptions options) {
  Layout<T> layout;
  const auto data = freezeData(value, layout);
  EXPECT_NO_THROW(inspectFrozenData(layout, byteSpan(data), options));
}

TEST(FrozenDataInspection, AcceptsValidFixedData) {
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

TEST(FrozenDataInspection, AcceptsValidOutOfLineData) {
  expectValid(std::string{"frozen string"});
  expectValid(WideStringForInspection{1, 2, 3});
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

TEST(FrozenDataInspection, AcceptsValidAssociativeData) {
  const DataInspectionOptions options{.checkAssociativeConsistency = true};
  expectValid(std::map<uint32_t, uint64_t>{{1, 10}, {2, 20}, {3, 30}}, options);
  expectValid(
      std::unordered_map<uint32_t, uint64_t>{{1, 10}, {2, 20}, {3, 30}},
      options);
}

TEST(FrozenDataInspection, AssociativeConsistencyRejectsUnorderedKeys) {
  using Value = std::map<uint32_t, uint32_t>;
  auto layout = maximumLayout<Value>();
  auto data = freezeData(Value{{1, 10}, {2, 20}}, layout);

  const auto second = rangeItemPosition(data, {}, layout, 1);

  writeField(data, second, layout.itemField.layout.firstField, uint32_t{0});

  EXPECT_NO_THROW(inspectFrozenData(layout, byteSpan(data)));
  EXPECT_THAT(
      [&] {
        inspectFrozenData(
            layout,
            byteSpan(data),
            DataInspectionOptions{.checkAssociativeConsistency = true});
      },
      ThrowsMessage<DataInspectionException>(AllOf(
          HasSubstr("[1]"),
          HasSubstr("ordered table keys are not strictly increasing"),
          HasSubstr("previous index=0"),
          HasSubstr("current index=1"),
          HasSubstr("expected previous key < current key"))));
}

TEST(FrozenDataInspection, AssociativeConsistencyRejectsDuplicateOrderedKeys) {
  using Value = std::map<uint32_t, uint32_t>;
  auto layout = maximumLayout<Value>();
  auto data = freezeData(Value{{1, 10}, {2, 20}}, layout);

  const auto second = rangeItemPosition(data, {}, layout, 1);

  writeField(data, second, layout.itemField.layout.firstField, uint32_t{1});

  EXPECT_NO_THROW(inspectFrozenData(layout, byteSpan(data)));
  EXPECT_THAT(
      [&] {
        inspectFrozenData(
            layout,
            byteSpan(data),
            DataInspectionOptions{.checkAssociativeConsistency = true});
      },
      ThrowsMessage<DataInspectionException>(AllOf(
          HasSubstr("[1]"),
          HasSubstr("ordered table keys are not strictly increasing"))));
}

TEST(FrozenDataInspection, RejectsNonEmptyHashTableWithoutBuckets) {
  using Value = std::unordered_map<uint32_t, uint32_t>;
  auto layout = maximumLayout<Value>();
  auto data = freezeData(Value{{1, 10}, {2, 20}}, layout);

  const auto sparseTable = fieldPosition({}, layout.sparseTableField);

  writeField(
      data, sparseTable, layout.sparseTableField.layout.countField, size_t{0});

  EXPECT_THAT(
      [&] { inspectFrozenData(layout, byteSpan(data)); },
      ThrowsMessage<DataInspectionException>(AllOf(
          HasSubstr(".sparseTable"),
          HasSubstr("non-empty hash table has no buckets"),
          HasSubstr("item count=2"),
          HasSubstr("bucket count=0"))));
}

TEST(FrozenDataInspection, RejectsNonCanonicalHashBlockOffset) {
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
      [&] { inspectFrozenData(layout, byteSpan(data)); },
      ThrowsMessage<DataInspectionException>(AllOf(
          HasSubstr(".sparseTable[0].offset"),
          HasSubstr(
              "hash table block offset does not match preceding population"),
          HasSubstr("actual=1"),
          HasSubstr("expected=0"))));
}

TEST(FrozenDataInspection, RejectsHashBlockPopulationBeyondItems) {
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
      [&] { inspectFrozenData(layout, byteSpan(data)); },
      ThrowsMessage<DataInspectionException>(AllOf(
          HasSubstr(".sparseTable[0].mask"),
          HasSubstr("hash table block population exceeds the item count"),
          HasSubstr("population=64"),
          HasSubstr("item count=2"))));
}

TEST(FrozenDataInspection, RejectsHashPopulationBelowItemCount) {
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
      [&] { inspectFrozenData(layout, byteSpan(data)); },
      ThrowsMessage<DataInspectionException>(AllOf(
          HasSubstr(".sparseTable"),
          HasSubstr("hash table population does not match the item count"),
          HasSubstr("actual=1"),
          HasSubstr("expected=2"))));
}

TEST(FrozenDataInspection, AssociativeConsistencyRejectsInvalidHashIndex) {
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

  EXPECT_NO_THROW(inspectFrozenData(layout, byteSpan(data)));
  EXPECT_THAT(
      [&] {
        inspectFrozenData(
            layout,
            byteSpan(data),
            DataInspectionOptions{.checkAssociativeConsistency = true});
      },
      ThrowsMessage<DataInspectionException>(AllOf(
          HasSubstr("[0]"),
          AnyOf(
              HasSubstr("hash table key is not reachable through its index"),
              HasSubstr("hash table key resolves to a different item")),
          HasSubstr("expected item index=0"))));
}

TEST(FrozenDataInspection, AssociativeConsistencyRejectsDuplicateHashKeys) {
  using Value = std::unordered_map<uint32_t, uint32_t>;
  auto layout = maximumLayout<Value>();
  auto data = freezeData(Value{{1, 10}, {2, 20}}, layout);

  const auto first = rangeItemPosition(data, {}, layout, 0);
  const auto second = rangeItemPosition(data, {}, layout, 1);
  const auto& keyField = layout.itemField.layout.firstField;
  const auto firstKey = readField(data, first, keyField);

  writeField(data, second, keyField, firstKey);

  EXPECT_NO_THROW(inspectFrozenData(layout, byteSpan(data)));
  EXPECT_THAT(
      [&] {
        inspectFrozenData(
            layout,
            byteSpan(data),
            DataInspectionOptions{.checkAssociativeConsistency = true});
      },
      ThrowsMessage<DataInspectionException>(AllOf(
          HasSubstr("[1]"),
          HasSubstr("hash table key resolves to a different item"),
          HasSubstr("expected item index=1"))));
}

TEST(FrozenDataInspection, CollectsPhysicalRegions) {
  const std::vector<std::string> value{"one", "", "three"};
  Layout<std::vector<std::string>> layout;
  const auto data = freezeData(value, layout);

  const auto result = inspectFrozenData(layout, byteSpan(data));
  ASSERT_EQ(4, result.regions.size());

  const auto& root = result.regions[0];
  EXPECT_EQ(&layout, root.layout);
  EXPECT_EQ(DataRegionKind::RootObject, root.kind);
  EXPECT_EQ(0, root.offset);
  EXPECT_EQ(layout.size != 0 ? layout.size : (layout.bits + 7) / 8, root.size);

  const auto& items = result.regions[1];
  EXPECT_EQ(&layout, items.layout);
  EXPECT_EQ(DataRegionKind::RangeItems, items.kind);
  EXPECT_EQ(rangeDataPosition(data, {}, layout).byteOffset, items.offset);
  EXPECT_EQ(value.size(), items.elementCount);
  EXPECT_EQ(layout.itemField.layout.size, items.elementByteStride);
  EXPECT_EQ(
      layout.itemField.layout.size == 0 ? layout.itemField.layout.bits : 0,
      items.elementBitStride);

  const auto& first = result.regions[2];
  EXPECT_EQ(&layout.itemField.layout, first.layout);
  EXPECT_EQ(DataRegionKind::StringBytes, first.kind);
  EXPECT_EQ(rangeItemPosition(data, {}, layout, 0), first.objectPosition);
  EXPECT_EQ(value[0].size(), first.size);
  EXPECT_EQ(value[0].size(), first.elementCount);
  EXPECT_EQ(1, first.elementByteStride);

  const auto& third = result.regions[3];
  EXPECT_EQ(&layout.itemField.layout, third.layout);
  EXPECT_EQ(DataRegionKind::StringBytes, third.kind);
  EXPECT_EQ(rangeItemPosition(data, {}, layout, 2), third.objectPosition);
  EXPECT_EQ(value[2].size(), third.size);
}

TEST(FrozenDataInspection, CollectsZeroSizeRangeRegion) {
  const std::vector<int32_t> value(100, 0);
  Layout<std::vector<int32_t>> layout;
  const auto data = freezeData(value, layout);

  const auto result = inspectFrozenData(layout, byteSpan(data));
  ASSERT_EQ(2, result.regions.size());
  const auto& items = result.regions[1];
  EXPECT_EQ(DataRegionKind::RangeItems, items.kind);
  EXPECT_EQ(value.size(), items.elementCount);
  EXPECT_EQ(0, items.size);
  EXPECT_EQ(0, items.elementByteStride);
  EXPECT_EQ(0, items.elementBitStride);
}

TEST(FrozenDataInspection, AcceptsRangeWithZeroStorageItems) {
  expectValid(std::vector<int32_t>(100, 0));
  expectValid(std::vector<std::pair<int32_t, int32_t>>(100));
}

TEST(FrozenDataInspection, SkipsInspectionForFixedRangeItems) {
  const std::vector<FixedInspectionProbe> value{{1}, {2}, {3}};
  Layout<std::vector<FixedInspectionProbe>> layout;
  const auto data = freezeData(value, layout);

  Layout<FixedInspectionProbe>::inspectionCount = 0;
  EXPECT_NO_THROW(inspectFrozenData(layout, byteSpan(data)));
  EXPECT_EQ(0, Layout<FixedInspectionProbe>::inspectionCount);
}

TEST(FrozenDataInspection, InspectsEveryDynamicRangeItem) {
  const std::vector<DynamicInspectionProbe> value{{1}, {2}, {3}};
  Layout<std::vector<DynamicInspectionProbe>> layout;
  const auto data = freezeData(value, layout);

  Layout<DynamicInspectionProbe>::inspectionCount = 0;
  EXPECT_NO_THROW(inspectFrozenData(layout, byteSpan(data)));
  EXPECT_EQ(value.size(), Layout<DynamicInspectionProbe>::inspectionCount);
}

TEST(FrozenDataInspection, AcceptsMisalignedPackedMapItems) {
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

  EXPECT_NO_THROW(inspectFrozenData(layout, relocated));
  EXPECT_EQ(value, layout.view({relocated.data(), 0}).thaw());
}

TEST(FrozenDataInspection, RejectsMisalignedRelocatedBlitRange) {
  const std::vector<double> value{1.0, 2.0, 3.0};
  Layout<std::vector<double>> layout;
  const auto data = freezeData(value, layout);
  const auto distance = readField(data, {}, layout.distanceField);

  std::vector<byte> storage;
  const auto relocated =
      relocateWithMisalignedRangeData(data, distance, alignof(double), storage);

  EXPECT_THAT(
      [&] { inspectFrozenData(layout, relocated); },
      ThrowsMessage<DataInspectionException>(AllOf(
          HasSubstr("range data is not correctly aligned"),
          HasSubstr("expected alignment=" + std::to_string(alignof(double))))));
}

TEST(FrozenDataInspection, AcceptsGeneratedStructData) {
  Person1 person;
  *person.name() = "person";
  *person.height() = 1.75;
  person.age() = 42;
  auto& pet = person.pets()->emplace_back();
  *pet.name() = "pet";

  expectValid(person);
  expectValid(Empty{});
}

TEST(FrozenDataInspection, RejectsInvalidGeneratedStructFieldData) {
  Person1 person;
  *person.name() = "person";
  auto& pet = person.pets()->emplace_back();
  *pet.name() = "pet";

  auto layout = maximumLayout<Person1>();
  auto data = freezeData(person, layout);
  const auto pets = fieldPosition(DataPosition{}, layout.petsField);
  const auto petsDistance =
      readField(data, pets, layout.petsField.layout.distanceField);
  const DataPosition petPos{.byteOffset = pets.byteOffset + petsDistance};
  const auto name =
      fieldPosition(petPos, layout.petsField.layout.itemField.layout.nameField);

  writeField(
      data,
      name,
      layout.petsField.layout.itemField.layout.nameField.layout.distanceField,
      data.size() - LayoutRoot::kPaddingBytes);

  EXPECT_THAT(
      [&] { inspectFrozenData(layout, byteSpan(data)); },
      ThrowsMessage<DataInspectionException>(AllOf(
          HasSubstr(".pets[0].name"),
          HasSubstr("string data extends beyond the frozen data range"),
          HasSubstr("logical size="))));
}

TEST(FrozenDataInspection, RejectsStringDataOutsideFrozenRange) {
  auto layout = maximumLayout<std::string>();
  auto data = freezeData(std::string{"data"}, layout);

  writeField(
      data, {}, layout.distanceField, data.size() - LayoutRoot::kPaddingBytes);

  EXPECT_THAT(
      [&] { inspectFrozenData(layout, byteSpan(data)); },
      ThrowsMessage<DataInspectionException>(
          HasSubstr("string data extends beyond the frozen data range")));
}

TEST(FrozenDataInspection, RejectsStringDataOverlappingRoot) {
  auto layout = maximumLayout<std::string>();
  auto data = freezeData(std::string{"data"}, layout);

  writeField(data, {}, layout.distanceField, size_t{0});

  EXPECT_THAT(
      [&] { inspectFrozenData(layout, byteSpan(data)); },
      ThrowsMessage<DataInspectionException>(
          HasSubstr("string data overlaps root object")));
}

TEST(FrozenDataInspection, RejectsStringDataSizeOverflow) {
  auto layout = maximumLayout<WideStringForInspection>();
  auto data = freezeData(WideStringForInspection{1}, layout);

  writeField(data, {}, layout.countField, std::numeric_limits<size_t>::max());

  EXPECT_THAT(
      [&] { inspectFrozenData(layout, byteSpan(data)); },
      ThrowsMessage<DataInspectionException>(
          HasSubstr("string data overflows while computing its size")));
}

TEST(FrozenDataInspection, RejectsMisalignedStringData) {
  auto layout = maximumLayout<WideStringForInspection>();
  auto data = freezeData(WideStringForInspection{1}, layout);
  const auto distance = readField(data, {}, layout.distanceField);
  data.insert(data.end() - LayoutRoot::kPaddingBytes, '\0');

  writeField(data, {}, layout.distanceField, distance + 1);

  EXPECT_THAT(
      [&] { inspectFrozenData(layout, byteSpan(data)); },
      ThrowsMessage<DataInspectionException>(
          HasSubstr("string data is not correctly aligned")));
}

TEST(FrozenDataInspection, RejectsStringDataPositionOverflow) {
  using Value = std::vector<std::string>;
  auto layout = maximumLayout<Value>();
  auto data = freezeData(Value{"data"}, layout);
  const auto itemOffset = readField(data, {}, layout.distanceField);
  const DataPosition item{.byteOffset = itemOffset};

  writeField(
      data,
      item,
      layout.itemField.layout.distanceField,
      std::numeric_limits<size_t>::max());

  EXPECT_THAT(
      [&] { inspectFrozenData(layout, byteSpan(data)); },
      ThrowsMessage<DataInspectionException>(
          HasSubstr("string data position overflows while adding offsets")));
}

TEST(FrozenDataInspection, RejectsRangeDataOutsideFrozenRange) {
  auto layout = maximumLayout<std::vector<int32_t>>();
  auto data = freezeData(std::vector<int32_t>{1, 2}, layout);

  writeField(
      data, {}, layout.distanceField, data.size() - LayoutRoot::kPaddingBytes);

  EXPECT_THAT(
      [&] { inspectFrozenData(layout, byteSpan(data)); },
      ThrowsMessage<DataInspectionException>(
          HasSubstr("range data extends beyond the frozen data range")));
}

TEST(FrozenDataInspection, RejectsRangeDataOverlappingRoot) {
  auto layout = maximumLayout<std::vector<int32_t>>();
  auto data = freezeData(std::vector<int32_t>{1, 2}, layout);

  writeField(data, {}, layout.distanceField, size_t{0});

  EXPECT_THAT(
      [&] { inspectFrozenData(layout, byteSpan(data)); },
      ThrowsMessage<DataInspectionException>(
          HasSubstr("range data overlaps root object")));
}

TEST(FrozenDataInspection, RejectsZeroStorageRangePositionOutsideData) {
  using Value = std::vector<int32_t>;
  Value value(4, 0);
  Layout<Value> layout;
  layout.distanceField.layout.bits = sizeof(size_t) * 8;
  layout.distanceField.layout.inlined = true;
  auto data = freezeData(value, layout);

  ASSERT_TRUE(layout.itemField.layout.empty());

  writeField(data, {}, layout.distanceField, data.size() + 1);

  EXPECT_THAT(
      [&] { inspectFrozenData(layout, byteSpan(data)); },
      ThrowsMessage<DataInspectionException>(HasSubstr(
          "range data position performs a read beyond the frozen data range")));
}

TEST(FrozenDataInspection, RejectsRangeDataPositionOverflow) {
  using Value = std::vector<std::vector<int32_t>>;
  auto layout = maximumLayout<Value>();
  auto data = freezeData(Value{{1, 2}}, layout);
  const auto itemOffset = readField(data, {}, layout.distanceField);
  const DataPosition item{.byteOffset = itemOffset};

  writeField(
      data,
      item,
      layout.itemField.layout.distanceField,
      std::numeric_limits<size_t>::max());

  EXPECT_THAT(
      [&] { inspectFrozenData(layout, byteSpan(data)); },
      ThrowsMessage<DataInspectionException>(
          HasSubstr("range data position overflows while adding offsets")));
}

TEST(FrozenDataInspection, RejectsRangeDataSizeOverflow) {
  auto layout = maximumLayout<std::vector<double>>();
  auto data = freezeData(std::vector<double>{1.0}, layout);

  writeField(data, {}, layout.countField, std::numeric_limits<size_t>::max());

  EXPECT_THAT(
      [&] { inspectFrozenData(layout, byteSpan(data)); },
      ThrowsMessage<DataInspectionException>(
          HasSubstr("range data overflows while computing its size")));
}

TEST(FrozenDataInspection, RejectsMisalignedRangeData) {
  auto layout = maximumLayout<std::vector<double>>();
  auto data = freezeData(std::vector<double>{1.0}, layout);
  const auto distance = readField(data, {}, layout.distanceField);
  data.insert(data.end() - LayoutRoot::kPaddingBytes, '\0');

  writeField(data, {}, layout.distanceField, distance + 1);

  EXPECT_THAT(
      [&] { inspectFrozenData(layout, byteSpan(data)); },
      ThrowsMessage<DataInspectionException>(AllOf(
          HasSubstr("range data is not correctly aligned"),
          HasSubstr("actual remainder="),
          HasSubstr("expected alignment=" + std::to_string(alignof(double))))));
}

TEST(FrozenDataInspection, RejectsOverlappingStringPayloads) {
  using Value = std::pair<std::string, std::string>;
  auto layout = maximumLayout<Value>();
  auto data = freezeData(Value{"first", "second"}, layout);

  const auto first = fieldPosition(DataPosition{}, layout.firstField);
  const auto second = fieldPosition(DataPosition{}, layout.secondField);
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
      [&] { inspectFrozenData(layout, byteSpan(data)); },
      ThrowsMessage<DataInspectionException>(AllOf(
          HasSubstr(".second"),
          HasSubstr("string data overlaps string data"),
          HasSubstr("actual interval=["),
          HasSubstr("conflicting location="),
          HasSubstr(".first"))));
}

TEST(FrozenDataInspection, RejectsInvalidNestedStringPayload) {
  using Value = std::vector<std::string>;
  auto layout = maximumLayout<Value>();
  auto data = freezeData(Value{"nested"}, layout);

  const auto rangeDistance = readField(data, {}, layout.distanceField);
  const DataPosition item{.byteOffset = rangeDistance};

  writeField(
      data,
      item,
      layout.itemField.layout.distanceField,
      data.size() - LayoutRoot::kPaddingBytes);

  EXPECT_THAT(
      [&] { inspectFrozenData(layout, byteSpan(data)); },
      ThrowsMessage<DataInspectionException>(
          HasSubstr("string data extends beyond the frozen data range")));
}

TEST(FrozenDataInspection, AcceptsEmptyRoot) {
  Layout<int32_t> layout;
  std::array<byte, LayoutRoot::kPaddingBytes> data{};

  EXPECT_NO_THROW(inspectFrozenData(layout, data));
}

TEST(FrozenDataInspection, RejectsMissingPackedReadPadding) {
  Layout<int32_t> layout;
  std::array<byte, LayoutRoot::kPaddingBytes - 1> data{};

  EXPECT_THAT(
      [&] { inspectFrozenData(layout, data); },
      ThrowsMessage<DataInspectionException>(
          HasSubstr("missing the trailing packed-read padding")));
}

TEST(FrozenDataInspection, RejectsRootOutsideLogicalData) {
  Layout<float> layout;
  auto data = freezeData(1.0f, layout);

  ASSERT_GT(data.size(), LayoutRoot::kPaddingBytes);
  data.erase(data.end() - LayoutRoot::kPaddingBytes - 1);

  EXPECT_THAT(
      [&] { inspectFrozenData(layout, byteSpan(data)); },
      ThrowsMessage<DataInspectionException>(
          HasSubstr("root object extends beyond the frozen data range")));
}

TEST(FrozenDataInspection, RejectsByteLayoutWithDataBitOffset) {
  detail::Block value{.mask = 0x1234, .offset = 7};
  Layout<detail::Block> layout;
  const auto data = freezeData(value, layout);

  // mask is a byte layout. Corrupt its loaded position so BlockLayout's
  // recursive inspection reaches LayoutBase::inspectData() with a bit offset.
  layout.maskField.pos = FieldPosition{0, 1};

  EXPECT_THAT(
      [&] { inspectFrozenData(layout, byteSpan(data)); },
      ThrowsMessage<DataInspectionException>(AllOf(
          HasSubstr(".mask"),
          HasSubstr("byte layout has a non-zero data bit offset"),
          HasSubstr("actual=1"),
          HasSubstr("expected=0"))));
}

TEST(FrozenDataInspection, RetainsDataInspectionOptions) {
  std::array<byte, 2 * LayoutRoot::kPaddingBytes> data{};
  DataInspectionContext context(
      data, DataInspectionOptions{.checkAssociativeConsistency = true});

  EXPECT_TRUE(context.options().checkAssociativeConsistency);
  EXPECT_EQ(LayoutRoot::kPaddingBytes, context.logicalSize());
}

TEST(FrozenDataInspection, RejectsPositionOverflow) {
  std::array<byte, LayoutRoot::kPaddingBytes> data{};
  DataInspectionContext context(data);

  EXPECT_THAT(
      [&] {
        context.position(
            {std::numeric_limits<size_t>::max(), 0}, FieldPosition{1, 0});
      },
      ThrowsMessage<DataInspectionException>(
          HasSubstr("field position overflows while adding offsets")));
  EXPECT_THAT(
      [&] {
        context.position(
            {0, std::numeric_limits<size_t>::max()}, FieldPosition{0, 1});
      },
      ThrowsMessage<DataInspectionException>(
          HasSubstr("field bit position overflows while adding offsets")));
}

TEST(FrozenDataInspection, RejectsInvalidLoadedFieldPosition) {
  std::array<byte, LayoutRoot::kPaddingBytes> data{};
  DataInspectionContext context(data);

  EXPECT_THAT(
      [&] { context.position({}, FieldPosition{-1, 0}); },
      ThrowsMessage<DataInspectionException>(
          HasSubstr("invalid loaded field position")));
  EXPECT_THAT(
      [&] { context.position({}, FieldPosition{0, -1}); },
      ThrowsMessage<DataInspectionException>(
          HasSubstr("invalid loaded field position")));
}

TEST(FrozenDataInspection, RejectsPhysicalReadOutsideData) {
  std::array<byte, LayoutRoot::kPaddingBytes> data{};
  DataInspectionContext context(data);

  EXPECT_THAT(
      [&] {
        context.requirePhysicalBytes(data.size(), 1, "test physical read");
      },
      ThrowsMessage<DataInspectionException>(HasSubstr(
          "test physical read performs a read beyond the frozen data range")));
}

TEST(FrozenDataInspection, AcceptsZeroBitPackedRead) {
  std::array<byte, LayoutRoot::kPaddingBytes> data{};
  DataInspectionContext context(data);

  // Zero-bit layouts perform no read, so neither the position nor word size
  // needs to be inspected.
  EXPECT_NO_THROW(context.requirePackedRead(
      {std::numeric_limits<size_t>::max(), std::numeric_limits<size_t>::max()},
      0,
      3,
      "zero-bit packed read"));
}

TEST(FrozenDataInspection, RejectsInvalidPackedReadWordSize) {
  std::array<byte, LayoutRoot::kPaddingBytes> data{};
  DataInspectionContext context(data);

  EXPECT_THAT(
      [&] { context.requirePackedRead({}, 1, 3, "test packed read"); },
      ThrowsMessage<DataInspectionException>(AllOf(
          HasSubstr("invalid packed-read word size"),
          HasSubstr("actual=3"),
          HasSubstr("expected a non-zero power of two"))));
}

TEST(FrozenDataInspection, RejectsInvalidAlignmentRequirement) {
  std::array<byte, LayoutRoot::kPaddingBytes> data{};
  DataInspectionContext context(data);

  EXPECT_THAT(
      [&] { context.requireAlignment(0, 3, "test allocation"); },
      ThrowsMessage<DataInspectionException>(AllOf(
          HasSubstr("invalid frozen-data alignment"),
          HasSubstr("actual=3"),
          HasSubstr("expected a non-zero power of two"))));
}

TEST(FrozenDataInspection, RejectsSizeOverflow) {
  std::array<byte, LayoutRoot::kPaddingBytes> data{};
  DataInspectionContext context(data);

  EXPECT_THAT(
      [&] {
        context.checkedMultiply(
            std::numeric_limits<size_t>::max(), 2, "test allocation");
      },
      ThrowsMessage<DataInspectionException>(
          HasSubstr("test allocation overflows while computing its size")));
}

TEST(FrozenDataInspection, RejectsOverlappingRegions) {
  std::array<byte, 24> data{};
  DataInspectionContext context(data);
  Layout<uint8_t> layout;

  registerTestRegion(context, layout, 0, 8, "first region");
  EXPECT_THAT(
      [&] { registerTestRegion(context, layout, 4, 8, "second region"); },
      ThrowsMessage<DataInspectionException>(
          HasSubstr("second region overlaps first region")));
}

TEST(FrozenDataInspection, RejectsRegionOverlappingFollowingRegion) {
  std::array<byte, 24> data{};
  DataInspectionContext context(data);
  Layout<uint8_t> layout;

  registerTestRegion(context, layout, 8, 8, "following region");
  EXPECT_THAT(
      [&] { registerTestRegion(context, layout, 4, 8, "preceding region"); },
      ThrowsMessage<DataInspectionException>(
          HasSubstr("preceding region overlaps following region")));
}

TEST(FrozenDataInspection, AcceptsAdjacentAndEmptyRegions) {
  std::array<byte, 24> data{};
  DataInspectionContext context(data);
  Layout<uint8_t> layout;

  registerTestRegion(context, layout, 0, 8, "first region");
  EXPECT_NO_THROW(registerTestRegion(context, layout, 8, 8, "second region"));
  EXPECT_NO_THROW(
      registerTestRegion(context, layout, 0, 0, "empty allocation"));
}

TEST(FrozenDataInspection, RejectsMisalignedData) {
  std::array<byte, 24> data{};
  DataInspectionContext context(std::span<const byte>(data).subspan(1));

  const auto base = reinterpret_cast<uintptr_t>(data.data() + 1);
  size_t offset = 0;
  while (((base + offset) & 7U) == 0) {
    ++offset;
  }
  EXPECT_THAT(
      [&] { context.requireAlignment(offset, 8, "test allocation"); },
      ThrowsMessage<DataInspectionException>(
          HasSubstr("test allocation is not correctly aligned")));
}

} // namespace
} // namespace apache::thrift::frozen
