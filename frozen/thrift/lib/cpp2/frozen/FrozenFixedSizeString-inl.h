/*
 * SPDX-FileCopyrightText: Copyright (c) Meta Platforms, Inc. and affiliates.
 * SPDX-FileCopyrightText: Copyright (c) Marcus Holland-Moritz
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * This file is derived from fbthrift and has been modified by
 * Marcus Holland-Moritz for use in dwarfs.
 */

#include <cassert>

namespace apache {
namespace thrift {
namespace frozen {
namespace detail {

class FixedSizeMismatchException : public std::length_error {
 public:
  FixedSizeMismatchException(size_t expected, size_t actual)
      : std::length_error(
            "Size mismatch. FixedSizeString specifies " +
            std::to_string(expected) + ", actual size is " +
            std::to_string(actual)) {}
};

/**
 * Serializes a string blob with a fixed size. Similar to TrivialLayout, but
 * uses std::span<uint8_t const> as the view. During freezing, an exception will
 * be thrown if the actual size doesn't match what's specified in the IDL
 * schema.
 */
template <typename T>
struct FixedSizeStringLayout : public LayoutBase {
  using Base = LayoutBase;
  FixedSizeStringLayout() : LayoutBase(typeid(T)) {}

  FieldPosition maximize() { return FieldPosition(T::kFixedSize, 0); }

  FieldPosition layout(LayoutRoot&, const T&, LayoutPosition /* start */) {
    return maximize();
  }

  void freeze(FreezeRoot&, const T& o, FreezePosition self) const {
    if (o.size() == T::kFixedSize) {
      memcpy(self.start, o.data(), o.size());
    } else {
      throw FixedSizeMismatchException(T::kFixedSize, o.size());
    }
  }

  void thaw(ViewPosition self, T& out) const {
    if (size == T::kFixedSize) {
      out.resize(T::kFixedSize);
      memcpy(&out[0], self.start, T::kFixedSize);
    }
  }

  void print(std::ostream& os, int level) const override {
    LayoutBase::print(os, level);
    os << dwarfs::thrift_lite::demangle(type.name());
  }

  struct View final : public std::span<uint8_t const> {
   public:
    using std::span<uint8_t const>::span;

    View(std::span<uint8_t const> bytes) : std::span<uint8_t const>(bytes) {
      assert(bytes.size() == T::kFixedSize);
    }

    bool operator==(View rhs) const {
      return memcmp(this->data(), rhs.data(), T::kFixedSize) == 0;
    }

    std::string toString() const {
      return {reinterpret_cast<const char*>(this->data()), this->size()};
    }
  };

  View view(ViewPosition self) const { return View(self.start, T::kFixedSize); }

  static size_t hash(const T& value) {
    return FixedSizeStringHash<T::kFixedSize, T>::hash(value);
  }

  static size_t hash(const View& value) {
    return FixedSizeStringHash<T::kFixedSize, View>::hash(value);
  }
};

} // namespace detail

template <size_t kSize>
class FixedSizeString;

template <size_t kSize>
struct Layout<FixedSizeString<kSize>>
    : detail::FixedSizeStringLayout<FixedSizeString<kSize>> {};
} // namespace frozen
} // namespace thrift
} // namespace apache
