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
#include <limits>
#include <optional>
#include <string>
#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <thrift/lib/cpp2/frozen/Frozen.h>
#include <thrift/lib/cpp2/frozen/HintTypes.h>

namespace apache::thrift::frozen {
namespace {

using testing::HasSubstr;
using testing::ThrowsMessage;

std::span<const byte> byteSpan(const std::string& data) {
  return {reinterpret_cast<const byte*>(data.data()), data.size()};
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
      ThrowsMessage<DataValidationException>(
          HasSubstr("byte layout has a non-zero data bit offset")));
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
      ThrowsMessage<DataValidationException>(
          HasSubstr("invalid packed-read word size")));
}

TEST(FrozenDataValidation, RejectsInvalidAlignmentRequirement) {
  std::array<byte, LayoutRoot::kPaddingBytes> data{};
  DataValidationContext context(data);

  EXPECT_THAT(
      [&] { context.requireAlignment(0, 3, "test allocation"); },
      ThrowsMessage<DataValidationException>(
          HasSubstr("invalid frozen-data alignment")));
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
