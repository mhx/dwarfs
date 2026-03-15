/*
 * SPDX-FileCopyrightText: Copyright (c) Meta Platforms, Inc. and affiliates.
 * SPDX-FileCopyrightText: Copyright (c) Marcus Holland-Moritz
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * This file is derived from fbthrift and has been modified by
 * Marcus Holland-Moritz for use in dwarfs.
 */

#include <thrift/lib/cpp2/frozen/Frozen.h>

#include <utility>

namespace apache::thrift::frozen {

std::ostream& operator<<(std::ostream& os, DebugLine dl) {
  os << '\n';
  for (int i = 0; i < dl.level; ++i) {
    os << ' ' << ' ';
  }
  return os;
}

std::ostream& operator<<(std::ostream& os, const LayoutBase& layout) {
  layout.print(os, 0);
  return os;
}

bool LayoutBase::resize(FieldPosition after, bool _inlined) {
  bool resized = false;
  inlined = (this->size == 0 && _inlined);
  if (!inlined) {
    if (std::cmp_greater(after.offset, this->size)) {
      this->size = after.offset;
      resized = true;
    }
  }
  if (std::cmp_greater(after.bitOffset, this->bits)) {
    this->bits = after.bitOffset;
    resized = true;
  }
  return resized;
}

void LayoutBase::print(std::ostream& os, int level) const {
  os << DebugLine(level);
  if (size) {
    os << size << " byte";
    if (bits) {
      os << " (with " << bits << " bits)";
    }
  } else if (bits) {
    os << bits << " bit";
  } else {
    os << "empty";
  }
  os << ' ';
}

void LayoutBase::clear() {
  size = 0;
  bits = 0;
  inlined = false;
}

void ByteRangeFreezer::doAppendBytes(
    byte* origin,
    size_t n,
    std::span<uint8_t>& range,
    size_t& distance,
    size_t alignment) {
  TL_CHECK(origin <= write_.data(), "internal error");
  if (!n) {
    distance = 0;
    range = {};
    return;
  }
  auto start = reinterpret_cast<intptr_t>(write_.data());
  auto aligned = alignBy(start, alignment);
  auto padding = aligned - start;
  if (padding + n > write_.size()) {
    throw std::length_error("Insufficient buffer allocated");
  }
  range = write_.subspan(padding, n);
  write_ = write_.subspan(padding + n);
  distance = range.data() - origin;
}

namespace detail {

FieldPosition BlockLayout::maximize() {
  FieldPosition pos = startFieldPosition();
  FROZEN_MAXIMIZE_FIELD(mask);
  FROZEN_MAXIMIZE_FIELD(offset);
  return pos;
}

FieldPosition BlockLayout::layout(
    LayoutRoot& root, const T& x, LayoutPosition self) {
  FieldPosition pos = startFieldPosition();
  FROZEN_LAYOUT_FIELD_REQ(mask);
  FROZEN_LAYOUT_FIELD_REQ(offset);
  return pos;
}

void BlockLayout::freeze(
    FreezeRoot& root, const T& x, FreezePosition self) const {
  FROZEN_FREEZE_FIELD_REQ(mask);
  FROZEN_FREEZE_FIELD_REQ(offset);
}

void BlockLayout::print(std::ostream& os, int level) const {
  LayoutBase::print(os, level);
  os << dwarfs::thrift_lite::demangle(type.name());
  maskField.print(os, level + 1);
  offsetField.print(os, level + 1);
}

void BlockLayout::clear() {
  LayoutBase::clear();
  maskField.clear();
  offsetField.clear();
}

} // namespace detail
} // namespace apache::thrift::frozen
