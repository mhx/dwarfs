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
#include <span>
#include <string>

#include <gtest/gtest.h>

#include <thrift/lib/cpp2/frozen/Frozen.h>

namespace apache::thrift::frozen {
namespace {

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

// The serialized representation of scalar values must be identical on all
// hosts. Round-trip tests cannot verify this, because a native-endian
// writer and reader on the same host are self-consistent; only asserting
// the exact serialized byte pattern is host-independent. These tests fail
// on a big-endian host if the canonical little-endian byte order is not
// produced, and always pass on a little-endian host.

TEST(FrozenEndian, BlockMaskStoredLittleEndian) {
  // The hash-table block occupancy mask is frozen through
  // TrivialLayout<uint64_t> and is specified as a little-endian 64-bit
  // value occupying eight bytes.
  detail::Block block;
  block.mask = 0x0102030405060708ULL;
  block.offset = 7;

  Layout<detail::Block> layout;
  const auto data = freezeData(block, layout);

  ASSERT_EQ(sizeof(uint64_t), layout.maskField.layout.size);
  const auto maskOffset = static_cast<size_t>(layout.maskField.pos.offset);
  ASSERT_LE(maskOffset + sizeof(uint64_t), data.size());

  static constexpr std::array<uint8_t, 8> expected{
      0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01};
  EXPECT_EQ(
      0,
      std::memcmp(data.data() + maskOffset, expected.data(), expected.size()));

  // Reading the canonical bytes back must reproduce the value on any host.
  const auto view =
      layout.view({reinterpret_cast<const byte*>(data.data()), 0});
  EXPECT_EQ(block.mask, view.mask());
  EXPECT_EQ(block.offset, view.offset());
}

TEST(FrozenEndian, BlockMaskCrossEndianRead) {
  // Simulates reading a hash-table block written on a little-endian host:
  // the mask bytes are constructed by hand and must decode identically on
  // any architecture.
  detail::Block block;
  block.mask = 0;
  block.offset = 7;

  Layout<detail::Block> layout;
  auto data = freezeData(block, layout);

  // The mask value is all-zero above, so the layout reserves the full
  // eight bytes regardless; overwrite them with hand-written canonical
  // little-endian bytes for a distinctive value.
  ASSERT_EQ(sizeof(uint64_t), layout.maskField.layout.size);
  const auto maskOffset = static_cast<size_t>(layout.maskField.pos.offset);
  ASSERT_LE(maskOffset + sizeof(uint64_t), data.size());

  static constexpr std::array<uint8_t, 8> canonical{
      0x3C, 0x69, 0x96, 0x3C, 0x5A, 0x0F, 0xF0, 0xA5};
  std::memcpy(data.data() + maskOffset, canonical.data(), canonical.size());

  const auto view =
      layout.view({reinterpret_cast<const byte*>(data.data()), 0});
  EXPECT_EQ(0xA5F00F5A3C96693CULL, view.mask());
}

TEST(FrozenEndian, FloatingPointStoredLittleEndian) {
  // Floating-point values are stored through TrivialLayout and are
  // specified as little-endian IEEE-754.
  Layout<double> layout;
  const auto data = freezeData(1.0, layout);

  ASSERT_EQ(sizeof(double), layout.size);
  static constexpr std::array<uint8_t, 8> expected{
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x3F};
  ASSERT_LE(expected.size(), data.size());
  EXPECT_EQ(0, std::memcmp(data.data(), expected.data(), expected.size()));

  double thawed = 0.0;
  layout.thaw({reinterpret_cast<const byte*>(data.data()), 0}, thawed);
  EXPECT_EQ(1.0, thawed);
}

} // namespace
} // namespace apache::thrift::frozen
