/*
 * SPDX-FileCopyrightText: Copyright (c) Meta Platforms, Inc. and affiliates.
 * SPDX-FileCopyrightText: Copyright (c) Marcus Holland-Moritz
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * This file is derived from fbthrift and has been modified by
 * Marcus Holland-Moritz for use in dwarfs.
 */

#pragma once

#include <stdexcept>

#include <thrift/lib/cpp2/frozen/Frozen.h>

#include <dwarfs/thrift_lite/compact_reader.h>
#include <dwarfs/thrift_lite/compact_writer.h>

namespace apache::thrift::frozen {

namespace schema::frozen_constants {

constexpr std::int32_t kCurrentFrozenFileVersion() {
  return 1;
}

} // namespace schema::frozen_constants

class FrozenFileForwardIncompatible : public std::runtime_error {
 public:
  explicit FrozenFileForwardIncompatible(int fileVersion);

  int fileVersion() const { return fileVersion_; }
  int supportedVersion() const {
    return schema::frozen_constants::kCurrentFrozenFileVersion();
  }

 private:
  int fileVersion_;
};

/**
 * A FreezeRoot that mallocs buffers as needed.
 */
class MallocFreezer final : public FreezeRoot {
 public:
  explicit MallocFreezer() = default;

  template <class T>
  void freeze(const Layout<T>& layout, const T& root) {
    doFreeze(layout, root);
  }

  void appendTo(std::string& out) const {
    out.reserve(size_ + out.size());
    for (auto& segment : segments_) {
      out.append(segment.buffer, segment.buffer + segment.size);
    }
  }

 private:
  size_t distanceToEnd(const byte* ptr) const;

  std::span<uint8_t> appendBuffer(size_t size);

  void doAppendBytes(
      byte* origin,
      size_t n,
      std::span<uint8_t>& range,
      size_t& distance,
      size_t alignment) override;

  struct Segment {
    explicit Segment(size_t size);
    Segment(Segment&& other) noexcept : size(other.size), buffer(other.buffer) {
      other.buffer = nullptr;
    }

    ~Segment();

    size_t size{0};
    byte* buffer{nullptr}; // owned
  };
  std::map<const byte*, size_t> offsets_;
  std::vector<Segment> segments_;
  size_t size_{0};
};

[[nodiscard]] inline std::span<uint8_t> make_mutable_byte_span(
    std::string& str) {
  return {reinterpret_cast<uint8_t*>(str.data()), str.size()};
}

[[nodiscard]] inline std::span<uint8_t const> make_byte_span(
    std::string_view sv) {
  return {reinterpret_cast<uint8_t const*>(sv.data()), sv.size()};
}

/**
 * Returns an upper bound estimate of the number of bytes required to freeze
 * this object with a minimal layout. Actual bytes required will depend on the
 * alignment of the freeze buffer.
 */
template <class T>
size_t frozenSize(const T& v) {
  Layout<T> layout;
  return LayoutRoot::layout(v, layout) - LayoutRoot::kPaddingBytes;
}

/**
 * Returns an upper bound estimate of the number of bytes required to freeze
 * this object with a given layout. Actual bytes required will depend on on
 * the alignment of the freeze buffer.
 */
template <class T>
size_t frozenSize(const T& v, const Layout<T>& fixedLayout) {
  Layout<T> layout = fixedLayout;
  size_t size;
  bool changed;
  LayoutRoot::layout(v, layout, changed, size);
  if (changed) {
    throw LayoutException();
  }
  return size;
}

template <class T>
void serializeRootLayout(const Layout<T>& layout, std::string& out) {
  schema::MemorySchema memSchema;
  schema::Schema schema;
  saveRoot(layout, memSchema);
  schema::convert(memSchema, schema);

  *schema.fileVersion() = schema::frozen_constants::kCurrentFrozenFileVersion();
  std::vector<std::byte> buffer;
  ::dwarfs::thrift_lite::compact_writer writer(buffer);
  schema.write(writer);
  out.assign(reinterpret_cast<const char*>(buffer.data()), buffer.size());
}

template <class T>
void deserializeRootLayout(
    std::span<uint8_t const>& range, Layout<T>& layoutOut) {
  schema::Schema schema;
  ::dwarfs::thrift_lite::compact_reader reader(std::as_bytes(range));
  schema.read(reader);
  size_t schemaSize = reader.consumed_bytes();

  if (*schema.fileVersion() >
      schema::frozen_constants::kCurrentFrozenFileVersion()) {
    throw FrozenFileForwardIncompatible(*schema.fileVersion());
  }

  schema::MemorySchema memSchema;
  schema::convert(std::move(schema), memSchema);
  loadRoot(layoutOut, memSchema);
  range = range.subspan(schemaSize);
}

template <class T>
void freezeToString(const T& x, std::string& out) {
  out.clear();
  Layout<T> layout;
  size_t contentSize = LayoutRoot::layout(x, layout);
  serializeRootLayout(layout, out);

  size_t schemaSize = out.size();
  size_t bufferSize = schemaSize + contentSize;
  out.resize(bufferSize, 0);
  auto writeRange =
      make_mutable_byte_span(out).subspan(schemaSize, contentSize);
  ByteRangeFreezer::freeze(layout, x, writeRange);
  out.resize(out.size() - writeRange.size());
}

template <class T>
std::string freezeToString(const T& x) {
  std::string result;
  freezeToString(x, result);
  return result;
}

template <class T>
std::string freezeDataToString(const T& x, const Layout<T>& layout) {
  std::string out;
  MallocFreezer freezer;
  freezer.freeze(layout, x);
  freezer.appendTo(out);
  return out;
}

template <class T>
void freezeToStringMalloc(const T& x, std::string& out) {
  Layout<T> layout;
  LayoutRoot::layout(x, layout);
  out.clear();
  serializeRootLayout(layout, out);
  MallocFreezer freezer;
  freezer.freeze(layout, x);
  freezer.appendTo(out);
}

/**
 * mapFrozen<T>() returns an owned reference to a frozen object which can be
 * used as a Frozen view of the object. All overloads of this function return
 * MappedFrozen<T>, which is an alias for a type which bundles the view with its
 * associated resources. This type may be used directly to hold references to
 * mapped frozen objects.
 *
 * Depending on which overload is used, this bundle will hold references to
 * different associated data:
 *
 *  - mapFrozen<T>(ByteRange): Only the layout tree associated with the object.
 *  - mapFrozen<T>(StringPiece): Same as mapFrozen<T>(ByteRange).
 *  - mapFrozen<T>(MemoryMapping): Takes ownership of the memory mapping
 *      in addition to the layout tree.
 *  - mapFrozen<T>(File): Owns the memory mapping created from the File (which,
 *      in turn, takes ownership of the File) in addition to the layout tree.
 */
template <class T>
using MappedFrozen = Bundled<typename Layout<T>::View>;

template <class T>
MappedFrozen<T> mapFrozen(std::span<uint8_t const> range) {
  auto layout = std::make_unique<Layout<T>>();
  deserializeRootLayout(range, *layout);
  MappedFrozen<T> ret(layout->view({range.data(), 0}));
  ret.hold(std::move(layout));
  return ret;
}

template <class T>
MappedFrozen<T> mapFrozen(std::string_view range) {
  return mapFrozen<T>(make_byte_span(range));
}

/**
 * Maps from the given string, taking ownership of it and bundling it with the
 * return object to ensure its lifetime.
 * @param trim Trims the serialized layout from the input string.
 */
template <class T>
MappedFrozen<T> mapFrozen(std::string&& str, bool trim = true) {
  auto layout = std::make_unique<Layout<T>>();
  auto holder = std::make_unique<HolderImpl<std::string>>(std::move(str));
  auto& ownedStr = holder->t_;
  auto const rangeBefore = make_byte_span(ownedStr);
  auto range = rangeBefore;
  deserializeRootLayout(range, *layout);
  if (trim) {
    size_t trimSize = range.data() - rangeBefore.data();
    ownedStr.erase(ownedStr.begin(), ownedStr.begin() + trimSize);
    ownedStr.shrink_to_fit();
    range = make_byte_span(ownedStr);
  }
  MappedFrozen<T> ret(layout->view({range.data(), 0}));
  ret.holdImpl(std::move(holder));
  ret.hold(std::move(layout));
  return ret;
}

template <class T>
[[deprecated(
    "std::string values must be passed by move with std::move(str) or "
    "passed through non-owning StringPiece")]] MappedFrozen<T>
mapFrozen(const std::string& str) = delete;

} // namespace apache::thrift::frozen
