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

#include <functional>
#include <limits>
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

void LayoutBase::validate(LoadRoot&) const {
  const auto bitBytes = bits / 8 + static_cast<size_t>(bits % 8 != 0);
  if (size > 0 && bitBytes > size) {
    throw schema::SchemaValidationException(
        "frozen layout bit region does not fit in its byte size");
  }
}

void LayoutBase::validateData(
    DataValidationContext& context, DataValidationPosition position) const {
  if (empty()) {
    return;
  }
  if (size != 0) {
    if (position.bitOffset != 0) {
      throw DataValidationException(
          "byte layout has a non-zero data bit offset");
    }
    context.requireLogicalBytes(position.byteOffset, size, "frozen object");
  } else {
    context.requireLogicalBits(position, bits, "frozen object");
  }
}

DataValidationContext::DataValidationContext(
    std::span<const byte> data, ValidationOptions options)
    : data_(data), options_(options) {
  if (data.size() < LayoutRoot::kPaddingBytes) {
    throw DataValidationException(
        "frozen data is missing the trailing packed-read padding");
  }
  logicalSize_ = data.size() - LayoutRoot::kPaddingBytes;
}

size_t DataValidationContext::checkedAdd(
    size_t lhs, size_t rhs, std::string_view what) const {
  if (rhs > std::numeric_limits<size_t>::max() - lhs) {
    throw DataValidationException(
        std::string(what) + " overflows while adding offsets");
  }
  return lhs + rhs;
}

size_t DataValidationContext::checkedMultiply(
    size_t lhs, size_t rhs, std::string_view what) const {
  if (lhs != 0 && rhs > std::numeric_limits<size_t>::max() / lhs) {
    throw DataValidationException(
        std::string(what) + " overflows while computing its size");
  }
  return lhs * rhs;
}

DataValidationPosition DataValidationContext::position(
    DataValidationPosition parent, FieldPosition field) const {
  if (field.offset < 0 || field.bitOffset < 0 ||
      (field.offset != 0 && field.bitOffset != 0)) {
    throw DataValidationException("invalid loaded field position");
  }
  return {
      checkedAdd(
          parent.byteOffset,
          static_cast<size_t>(field.offset),
          "field position"),
      checkedAdd(
          parent.bitOffset,
          static_cast<size_t>(field.bitOffset),
          "field bit position")};
}

void DataValidationContext::requireLogicalBytes(
    size_t offset, size_t size, std::string_view what) const {
  if (offset > logicalSize_ || size > logicalSize_ - offset) {
    throw DataValidationException(
        std::string(what) + " extends beyond the frozen data range");
  }
}

void DataValidationContext::requireLogicalBits(
    DataValidationPosition position, size_t bits, std::string_view what) const {
  const auto endBit = checkedAdd(position.bitOffset, bits, what);
  const auto bytes = endBit / 8 + static_cast<size_t>(endBit % 8 != 0);
  requireLogicalBytes(position.byteOffset, bytes, what);
}

void DataValidationContext::requirePhysicalBytes(
    size_t offset, size_t size, std::string_view what) const {
  if (offset > data_.size() || size > data_.size() - offset) {
    throw DataValidationException(
        std::string(what) + " performs a read beyond the frozen data range");
  }
}

void DataValidationContext::requirePackedRead(
    DataValidationPosition position,
    size_t bits,
    size_t wordBytes,
    std::string_view what) const {
  if (bits == 0) {
    return;
  }
  if (!std::has_single_bit(wordBytes)) {
    throw DataValidationException("invalid packed-read word size");
  }

  const auto byteInObject = position.bitOffset >> 3;
  const auto chunkOffset = byteInObject & ~(wordBytes - 1);
  const auto bitInByte = position.bitOffset & 7;
  const auto shift = checkedAdd(
      checkedMultiply(byteInObject - chunkOffset, size_t{8}, what),
      bitInByte,
      what);
  const auto wordBits = checkedMultiply(wordBytes, size_t{8}, what);
  const auto readWords =
      checkedAdd(shift, bits, what) <= wordBits ? size_t{1} : size_t{2};
  const auto readOffset = checkedAdd(position.byteOffset, chunkOffset, what);
  const auto readSize = checkedMultiply(readWords, wordBytes, what);
  requirePhysicalBytes(readOffset, readSize, what);
}

void DataValidationContext::requireAlignment(
    size_t offset, size_t alignment, std::string_view what) const {
  if (!std::has_single_bit(alignment)) {
    throw DataValidationException("invalid frozen-data alignment");
  }
  requirePhysicalBytes(offset, 0, what);
  const auto address = reinterpret_cast<uintptr_t>(data_.data()) + offset;
  if ((address & (alignment - 1)) != 0) {
    throw DataValidationException(
        std::string(what) + " is not correctly aligned");
  }
}

void DataValidationContext::registerAllocation(
    size_t offset, size_t size, std::string_view what) {
  if (size == 0) {
    return;
  }

  requireLogicalBytes(offset, size, what);
  const auto end = offset + size;
  auto next = allocations_.lower_bound(offset);
  if (next != allocations_.end() && end > next->first) {
    throw DataValidationException(
        std::string(what) + " overlaps " + next->second.description);
  }
  if (next != allocations_.begin()) {
    const auto previous = std::prev(next);
    if (previous->second.end > offset) {
      throw DataValidationException(
          std::string(what) + " overlaps " + previous->second.description);
    }
  }

  allocations_.emplace(offset, Allocation{end, std::string(what)});
}

ViewPosition DataValidationContext::viewPosition(
    DataValidationPosition position) const {
  requirePhysicalBytes(position.byteOffset, 0, "view position");
  const byte* start = nullptr;
  if (!data_.empty()) {
    start = data_.data() + position.byteOffset;
  }
  return {start, position.bitOffset};
}

void LoadRoot::registerField(
    const LayoutBase& parent,
    const FieldBase& field,
    const LayoutBase& child,
    int16_t encodedOffset,
    FieldPlacement placement) {
  if (placement == FieldPlacement::OutOfLine) {
    return;
  }

  // Older schemas may retain fields whose layouts require no storage.
  // They have no interval to register, but their position must still remain
  // within the parent because field access forms a ViewPosition from it.
  if (child.empty()) {
    if (encodedOffset < 0) {
      const auto bitOffset =
          static_cast<size_t>(-static_cast<int32_t>(encodedOffset));
      if (bitOffset > parent.bits) {
        throw schema::SchemaValidationException(
            "empty field '" + std::string(field.name) +
            "' has a bit offset outside its parent layout");
      }
    } else if (static_cast<size_t>(encodedOffset) > parent.size) {
      throw schema::SchemaValidationException(
          "empty field '" + std::string(field.name) +
          "' has a byte offset outside its parent layout");
    }
    return;
  }

  size_t begin;
  size_t end;
  if (child.size > 0) {
    if (encodedOffset < 0) {
      throw schema::SchemaValidationException(
          "byte field '" + std::string(field.name) + "' has a bit offset");
    }

    const auto byteOffset = static_cast<size_t>(encodedOffset);
    if (byteOffset > parent.size || child.size > parent.size - byteOffset) {
      throw schema::SchemaValidationException(
          "byte field '" + std::string(field.name) +
          "' extends beyond its parent layout");
    }

    const auto bitBytes = (parent.bits + 7) / 8;
    if (byteOffset < bitBytes) {
      throw schema::SchemaValidationException(
          "byte field '" + std::string(field.name) +
          "' overlaps its parent's packed-bit region");
    }

    begin = byteOffset * 8;
    end = begin + child.size * 8;
  } else {
    if (encodedOffset > 0) {
      throw schema::SchemaValidationException(
          "bit field '" + std::string(field.name) + "' has a byte offset");
    }

    const auto bitOffset = encodedOffset < 0
        ? static_cast<size_t>(-static_cast<int32_t>(encodedOffset))
        : size_t{0};
    if (bitOffset > parent.bits || child.bits > parent.bits - bitOffset) {
      throw schema::SchemaValidationException(
          "bit field '" + std::string(field.name) +
          "' extends beyond its parent layout");
    }

    begin = bitOffset;
    end = begin + child.bits;
  }

  fields_.push_back({&parent, begin, end, field.name});
}

void LoadRoot::finish() {
  std::ranges::sort(fields_, [](const auto& lhs, const auto& rhs) {
    const auto less = std::less<const LayoutBase*>();
    if (less(lhs.parent, rhs.parent)) {
      return true;
    }
    if (less(rhs.parent, lhs.parent)) {
      return false;
    }
    if (lhs.begin != rhs.begin) {
      return lhs.begin < rhs.begin;
    }
    return lhs.end < rhs.end;
  });

  for (size_t i = 1; i < fields_.size(); ++i) {
    const auto& previous = fields_[i - 1];
    const auto& current = fields_[i];
    if (previous.parent == current.parent && current.begin < previous.end) {
      throw schema::SchemaValidationException(
          "fields '" + std::string(previous.name) + "' and '" +
          std::string(current.name) + "' overlap");
    }
  }
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

void BlockLayout::validateData(
    DataValidationContext& context, DataValidationPosition self) const {
  Base::validateData(context, self);
  validateDataField(context, self, maskField);
  validateDataField(context, self, offsetField);
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
