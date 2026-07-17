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

namespace apache::thrift::frozen {
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

  static constexpr bool kMayRequirePerItemInspection = false;

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
    } else {
      // Empty layout: the field was absent from the serialized schema
      // (e.g. a file written by an older version of the struct). Produce
      // the default value.
      out.clear();
    }
  }

  void print(
      std::ostream& os,
      const LayoutPrintOptions& options = {},
      int level = 0) const override {
    LayoutBase::print(os, options, level);
    os << dwarfs::thrift_lite::demangle(type.name());
  }

  void validate(LoadRoot& root) const override {
    Base::validate(root);
    if (!empty() && (size != T::kFixedSize || bits != 0)) {
      throw schema::SchemaValidationException(
          "invalid fixed-size string layout");
    }
  }

  void inspectData(
      DataInspectionContext& context, DataPosition position) const override {
    Base::inspectData(context, position);
    if (!empty()) {
      context.requirePhysicalBytes(
          position.byteOffset, T::kFixedSize, "fixed-size string");
    }
  }

  struct View : public std::span<uint8_t const> {
   public:
    using std::span<uint8_t const>::span;

    View(std::span<uint8_t const> bytes) : std::span<uint8_t const>(bytes) {
      assert(bytes.size() == T::kFixedSize || bytes.empty());
    }

    bool operator==(View rhs) const {
      if (this->size() != rhs.size()) {
        return false;
      }
      return this->empty() ||
          memcmp(this->data(), rhs.data(), this->size()) == 0;
    }

    std::string toString() const { return {this->begin(), this->end()}; }
  };

  View view(ViewPosition self) const {
    if (size != T::kFixedSize) {
      // Empty layout (field absent from the serialized schema, e.g. a file
      // written by an older version of the struct) or a corrupt size:
      // reading T::kFixedSize bytes here would trust the static type over
      // the schema and may read out of bounds. An empty view represents
      // the default value.
      return View{};
    }
    return View{std::span<uint8_t const>{self.start, T::kFixedSize}};
  }

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
} // namespace apache::thrift::frozen
