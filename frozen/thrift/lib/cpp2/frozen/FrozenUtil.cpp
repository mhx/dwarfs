/*
 * SPDX-FileCopyrightText: Copyright (c) Meta Platforms, Inc. and affiliates.
 * SPDX-FileCopyrightText: Copyright (c) Marcus Holland-Moritz
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * This file is derived from fbthrift and has been modified by
 * Marcus Holland-Moritz for use in dwarfs.
 */

#include <thrift/lib/cpp2/frozen/FrozenUtil.h>

#include <cstdlib>

namespace apache::thrift::frozen {

FrozenFileForwardIncompatible::FrozenFileForwardIncompatible(int fileVersion)
    : std::runtime_error(
          "Frozen File version " + std::to_string(fileVersion) +
          " cannot be read, only versions up to " +
          std::to_string(
              schema::frozen_constants::kCurrentFrozenFileVersion()) +
          " are supported."),
      fileVersion_(fileVersion) {}

// NOLINTBEGIN(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)
// TODO: revisit the use of malloc/free here
MallocFreezer::Segment::Segment(size_t _size)
    : size(_size),
      // NB: All allocations rounded up to next multiple of 8 due to packed
      // integer read amplification
      buffer(reinterpret_cast<byte*>(calloc(alignBy(size, 8), 1))) {
  if (!buffer) {
    throw std::runtime_error("Couldn't allocate memory");
  }
}

MallocFreezer::Segment::~Segment() {
  if (buffer) {
    free(buffer);
  }
}
// NOLINTEND(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)

size_t MallocFreezer::distanceToEnd(const byte* ptr) const {
  if (offsets_.empty()) {
    return 0;
  }
  auto offsetIt = offsets_.upper_bound(ptr);
  if (offsetIt == offsets_.begin()) {
    throw std::runtime_error("dist");
  }
  --offsetIt;
  TL_CHECK(ptr >= offsetIt->first, "internal error");
  return (size_ - offsetIt->second) - (ptr - offsetIt->first);
}

std::span<uint8_t> MallocFreezer::appendBuffer(size_t size) {
  Segment segment(size);
  offsets_.emplace(segment.buffer, size_);

  std::span<uint8_t> range(segment.buffer, size);
  size_ += segment.size;
  segments_.push_back(std::move(segment));
  return range;
}

void MallocFreezer::doAppendBytes(
    byte* origin,
    size_t n,
    std::span<uint8_t>& range,
    size_t& distance,
    size_t alignment) {
  if (!n) {
    distance = 0;
    range = {};
    return;
  }
  auto aligned = alignBy(size_, alignment);
  auto padding = aligned - size_;
  distance = distanceToEnd(origin) + padding;
  range = appendBuffer(padding + n);
  range = range.subspan(padding);
}
} // namespace apache::thrift::frozen
