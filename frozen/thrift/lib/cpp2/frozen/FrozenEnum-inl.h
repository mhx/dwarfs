/*
 * SPDX-FileCopyrightText: Copyright (c) Meta Platforms, Inc. and affiliates.
 * SPDX-FileCopyrightText: Copyright (c) Marcus Holland-Moritz
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * This file is derived from fbthrift and has been modified by
 * Marcus Holland-Moritz for use in dwarfs.
 */

namespace apache::thrift::frozen {
namespace detail {

/**
 * Layout specialization for enum values, simply delegates to Integer layout.
 */
template <
    class T,
    class Underlying =
        std::enable_if_t<std::is_enum_v<T>, std::underlying_type_t<T>>>
struct EnumLayout : public PackedIntegerLayout<Underlying> {
  using Base = PackedIntegerLayout<Underlying>;
  EnumLayout() : Base(typeid(T)) {}

  FieldPosition maximize() { return Base::maximize(); }

  FieldPosition layout(LayoutRoot& root, const T& o, LayoutPosition self) {
    return Base::layout(root, static_cast<Underlying>(o), self);
  }

  void freeze(FreezeRoot& root, const T& o, FreezePosition self) const {
    Base::freeze(root, static_cast<Underlying>(o), self);
  }

  void thaw(ViewPosition self, T& out) const {
    Underlying x;
    Base::thaw(self, x);
    out = T(x);
  }

  void print(std::ostream& os, int level) const override {
    Base::print(os, level);
    os << " as enum " << dwarfs::thrift_lite::demangle(this->type.name());
  }

  using View = T;

  View view(ViewPosition self) const {
    View v;
    thaw(self, v);
    return v;
  }

  static size_t hash(const T& v) { return std::hash<T>()(v); }
};
} // namespace detail

template <class T>
struct Layout<T, std::enable_if_t<std::is_enum_v<T>>>
    : public apache::thrift::frozen::detail::EnumLayout<T> {};
} // namespace apache::thrift::frozen
