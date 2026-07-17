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
  layout.print(os);
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

void LayoutBase::print(
    std::ostream& os, const LayoutPrintOptions& options, int level) const {
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
  if (options.includeSchemaLayoutIds && schemaLayoutId != kNoSchemaLayoutId) {
    os << "layout #" << schemaLayoutId << ' ';
  }
}

void LayoutBase::clear() {
  size = 0;
  bits = 0;
  inlined = false;
  schemaLayoutId = kNoSchemaLayoutId;
}

void LayoutBase::validate(LoadRoot&) const {
  const auto bitBytes = bits / 8 + static_cast<size_t>(bits % 8 != 0);
  if (size > 0 && bitBytes > size) {
    throw schema::SchemaValidationException(
        "frozen layout bit region does not fit in its byte size");
  }
}

void LayoutBase::inspectData(
    DataInspectionContext& context, DataPosition position) const {
  if (empty()) {
    return;
  }
  if (size != 0) {
    if (position.bitOffset != 0) {
      context.fail(
          "byte layout has a non-zero data bit offset: actual=" +
          std::to_string(position.bitOffset) + ", expected=0");
    }
    context.requireLogicalBytes(position.byteOffset, size, "frozen object");
  } else {
    context.requireLogicalBits(position, bits, "frozen object");
  }
}

DataInspectionContext::PathScope::PathScope(
    DataInspectionContext& context, PathComponent component)
    : context_(&context),
      pathSize_(context.path_.size()),
      previousPosition_(context.currentPosition_) {
  context.path_.push_back(component);
}

DataInspectionContext::PathScope::PathScope(PathScope&& other) noexcept
    : context_(std::exchange(other.context_, nullptr)),
      pathSize_(other.pathSize_),
      previousPosition_(other.previousPosition_) {}

DataInspectionContext::PathScope::~PathScope() {
  if (context_ != nullptr) {
    context_->path_.resize(pathSize_);
    context_->currentPosition_ = previousPosition_;
  }
}

void DataInspectionContext::PathScope::setPosition(
    DataPosition position) noexcept {
  context_->currentPosition_ = position;
}

DataInspectionContext::DataInspectionContext(
    std::span<const byte> data, DataInspectionOptions options)
    : data_(data), options_(options) {
  initialize();
}

DataInspectionContext::DataInspectionContext(
    std::span<const byte> data,
    std::type_index rootType,
    DataInspectionOptions options)
    : data_(data),
      options_(options),
      rootType_(dwarfs::thrift_lite::demangle(rootType.name())),
      currentPosition_(DataPosition{}) {
  initialize();
}

void DataInspectionContext::initialize() {
  if (data_.size() < LayoutRoot::kPaddingBytes) {
    fail(
        "frozen data is missing the trailing packed-read padding: actual "
        "size=" +
        std::to_string(data_.size()) +
        ", required minimum=" + std::to_string(LayoutRoot::kPaddingBytes));
  }
  logicalSize_ = data_.size() - LayoutRoot::kPaddingBytes;
}

DataInspectionContext::PathScope DataInspectionContext::pushField(
    std::string_view name) {
  return PathScope(*this, PathComponent{PathComponent::Kind::Field, name, 0});
}

DataInspectionContext::PathScope DataInspectionContext::pushIndex(
    size_t index) {
  return PathScope(*this, PathComponent{PathComponent::Kind::Index, {}, index});
}

std::string DataInspectionContext::path() const {
  std::string result = rootType_.empty() ? "<unknown>" : rootType_;
  for (const auto& component : path_) {
    switch (component.kind) {
      case PathComponent::Kind::Field:
        result += '.';
        result += component.field;
        break;
      case PathComponent::Kind::Index:
        result += '[';
        result += std::to_string(component.index);
        result += ']';
        break;
    }
  }
  return result;
}

[[noreturn]] void DataInspectionContext::fail(std::string message) const {
  std::string result = "at " + path();
  if (currentPosition_) {
    result += " (byte offset=" + std::to_string(currentPosition_->byteOffset) +
        ", bit offset=" + std::to_string(currentPosition_->bitOffset) + ')';
  }
  result += ": ";
  result += message;
  throw DataInspectionException(std::move(result));
}

size_t DataInspectionContext::checkedAdd(
    size_t lhs, size_t rhs, std::string_view what) const {
  if (rhs > std::numeric_limits<size_t>::max() - lhs) {
    fail(
        std::string(what) + " overflows while adding offsets: lhs=" +
        std::to_string(lhs) + ", rhs=" + std::to_string(rhs) +
        ", maximum=" + std::to_string(std::numeric_limits<size_t>::max()));
  }
  return lhs + rhs;
}

size_t DataInspectionContext::checkedMultiply(
    size_t lhs, size_t rhs, std::string_view what) const {
  if (lhs != 0 && rhs > std::numeric_limits<size_t>::max() / lhs) {
    fail(
        std::string(what) + " overflows while computing its size: lhs=" +
        std::to_string(lhs) + ", rhs=" + std::to_string(rhs) +
        ", maximum=" + std::to_string(std::numeric_limits<size_t>::max()));
  }
  return lhs * rhs;
}

DataPosition DataInspectionContext::position(
    DataPosition parent, FieldPosition field) const {
  if (field.offset < 0 || field.bitOffset < 0 ||
      (field.offset != 0 && field.bitOffset != 0)) {
    fail(
        "invalid loaded field position: byte offset=" +
        std::to_string(field.offset) +
        ", bit offset=" + std::to_string(field.bitOffset) +
        ", expected non-negative offsets with at most one non-zero value");
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

void DataInspectionContext::requireLogicalBytes(
    size_t offset, size_t size, std::string_view what) const {
  if (offset > logicalSize_ || size > logicalSize_ - offset) {
    fail(
        std::string(what) + " extends beyond the frozen data range: offset=" +
        std::to_string(offset) + ", size=" + std::to_string(size) +
        ", logical size=" + std::to_string(logicalSize_));
  }
}

void DataInspectionContext::requireLogicalBits(
    DataPosition position, size_t bits, std::string_view what) const {
  const auto endBit = checkedAdd(position.bitOffset, bits, what);
  const auto bytes = endBit / 8 + static_cast<size_t>(endBit % 8 != 0);
  requireLogicalBytes(position.byteOffset, bytes, what);
}

void DataInspectionContext::requirePhysicalBytes(
    size_t offset, size_t size, std::string_view what) const {
  if (offset > data_.size() || size > data_.size() - offset) {
    fail(
        std::string(what) +
        " performs a read beyond the frozen data range: offset=" +
        std::to_string(offset) + ", read size=" + std::to_string(size) +
        ", physical size=" + std::to_string(data_.size()));
  }
}

void DataInspectionContext::requirePackedRead(
    DataPosition position,
    size_t bits,
    size_t wordBytes,
    std::string_view what) const {
  if (bits == 0) {
    return;
  }
  if (!std::has_single_bit(wordBytes)) {
    fail(
        "invalid packed-read word size: actual=" + std::to_string(wordBytes) +
        ", expected a non-zero power of two");
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

void DataInspectionContext::requireAlignment(
    size_t offset, size_t alignment, std::string_view what) const {
  if (!std::has_single_bit(alignment)) {
    fail(
        "invalid frozen-data alignment: actual=" + std::to_string(alignment) +
        ", expected a non-zero power of two");
  }
  requirePhysicalBytes(offset, 0, what);
  const auto address = reinterpret_cast<uintptr_t>(data_.data()) + offset;
  const auto remainder = address & (alignment - 1);
  if (remainder != 0) {
    fail(
        std::string(what) + " is not correctly aligned: offset=" +
        std::to_string(offset) + ", address=" + std::to_string(address) +
        ", actual remainder=" + std::to_string(remainder) +
        ", expected alignment=" + std::to_string(alignment) +
        " and remainder=0");
  }
}

void DataInspectionContext::registerRegion(
    DataRegion region, std::string_view what) {
  if (region.layout == nullptr) {
    fail(std::string(what) + " has no owning layout");
  }

  if (region.size == 0) {
    requirePhysicalBytes(region.offset, 0, what);
    regions_.push_back(std::move(region));
    return;
  }

  requireLogicalBytes(region.offset, region.size, what);
  const auto end = region.offset + region.size;
  auto next = occupiedRegions_.lower_bound(region.offset);
  if (next != occupiedRegions_.end() && end > next->first) {
    fail(
        std::string(what) + " overlaps " + next->second.what +
        ": actual interval=[" + std::to_string(region.offset) + ", " +
        std::to_string(end) + "), conflicting interval=[" +
        std::to_string(next->first) + ", " + std::to_string(next->second.end) +
        "), conflicting location=" + next->second.location);
  }
  if (next != occupiedRegions_.begin()) {
    const auto previous = std::prev(next);
    if (previous->second.end > region.offset) {
      fail(
          std::string(what) + " overlaps " + previous->second.what +
          ": actual interval=[" + std::to_string(region.offset) + ", " +
          std::to_string(end) + "), conflicting interval=[" +
          std::to_string(previous->first) + ", " +
          std::to_string(previous->second.end) +
          "), conflicting location=" + previous->second.location);
    }
  }

  occupiedRegions_.emplace(
      region.offset, OccupiedRegion{end, std::string(what), path()});
  regions_.push_back(std::move(region));
}

DataInspectionResult DataInspectionContext::takeResult() && noexcept {
  return DataInspectionResult{.regions = std::move(regions_)};
}

ViewPosition DataInspectionContext::viewPosition(DataPosition position) const {
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

void BlockLayout::inspectData(
    DataInspectionContext& context, DataPosition self) const {
  Base::inspectData(context, self);
  inspectDataField(context, self, maskField);
  inspectDataField(context, self, offsetField);
}

void BlockLayout::print(
    std::ostream& os, const LayoutPrintOptions& options, int level) const {
  LayoutBase::print(os, options, level);
  os << dwarfs::thrift_lite::demangle(type.name());
  maskField.print(os, options, level + 1);
  offsetField.print(os, options, level + 1);
}

void BlockLayout::clear() {
  LayoutBase::clear();
  maskField.clear();
  offsetField.clear();
}

} // namespace detail
} // namespace apache::thrift::frozen
