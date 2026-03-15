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

/**
 * A view of an optional Frozen field. It provides a std::optional-like API
 * and intentionally disallows std::optional extensions.
 */
template <typename T>
class OptionalFieldView : private std::optional<T> {
 private:
  const std::optional<T>& toStd() const { return *this; }

 public:
  using std::optional<T>::operator->;
  using std::optional<T>::operator*;
  using std::optional<T>::operator bool;
  using std::optional<T>::has_value;
  using std::optional<T>::value;
  using std::optional<T>::value_or;
  using std::optional<T>::reset;
  using std::optional<T>::emplace;

  template <typename L, typename R>
  friend bool operator==(
      const OptionalFieldView<L>& lhs, const OptionalFieldView<R>& rhs);
  template <typename L, typename R>
  friend bool operator!=(
      const OptionalFieldView<L>& lhs, const OptionalFieldView<R>& rhs);

  template <typename L, typename R>
  friend bool operator==(const L& lhs, const OptionalFieldView<R>& rhs);
  template <typename L, typename R>
  friend bool operator==(const OptionalFieldView<L>& lhs, const R& rhs);
  template <typename L, typename R>
  friend bool operator!=(const L& lhs, const OptionalFieldView<R>& rhs);
  template <typename L, typename R>
  friend bool operator!=(const OptionalFieldView<L>& lhs, const R& rhs);

  template <typename U>
  friend bool operator==(
      const OptionalFieldView<U>& lhs, const std::optional<U>& rhs);
  template <typename U>
  friend bool operator==(
      const std::optional<U>& lhs, const OptionalFieldView<U>& rhs);
};

template <typename L, typename R>
bool operator==(
    const OptionalFieldView<L>& lhs, const OptionalFieldView<R>& rhs) {
  return lhs.toStd() == rhs.toStd();
}
template <typename L, typename R>
bool operator!=(
    const OptionalFieldView<L>& lhs, const OptionalFieldView<R>& rhs) {
  return lhs.toStd() != rhs.toStd();
}

template <typename L, typename R>
bool operator==(const L& lhs, const OptionalFieldView<R>& rhs) {
  return lhs == rhs.toStd();
}
template <typename L, typename R>
bool operator==(const OptionalFieldView<L>& lhs, const R& rhs) {
  return lhs.toStd() == rhs;
}
template <typename L, typename R>
bool operator!=(const L& lhs, const OptionalFieldView<R>& rhs) {
  return lhs != rhs.toStd();
}
template <typename L, typename R>
bool operator!=(const OptionalFieldView<L>& lhs, const R& rhs) {
  return lhs.toStd() != rhs;
}

namespace detail {

/**
 * Layout specialization for Optional<T>, which is used in the codegen for
 * optional fields. Just a boolean and a value.
 */
template <class T>
struct OptionalLayout : public LayoutBase {
  using Base = LayoutBase;
  Field<bool> issetField;
  Field<T> valueField;

  OptionalLayout()
      : LayoutBase(typeid(T)), issetField(1, "isset"), valueField(2, "value") {}

  FieldPosition maximize() {
    FieldPosition pos = startFieldPosition();
    FROZEN_MAXIMIZE_FIELD(isset);
    FROZEN_MAXIMIZE_FIELD(value);
    return pos;
  }

  FieldPosition layout(
      LayoutRoot& root, const std::optional<T>& o, LayoutPosition self) {
    FieldPosition pos = startFieldPosition();
    pos = root.layoutField(self, pos, issetField, o.has_value());
    if (o) {
      pos = root.layoutField(self, pos, valueField, o.value());
    }
    return pos;
  }

  FieldPosition layout(LayoutRoot& root, const T& o, LayoutPosition self) {
    FieldPosition pos = startFieldPosition();
    pos = root.layoutField(self, pos, issetField, true);
    pos = root.layoutField(self, pos, valueField, o);
    return pos;
  }

  void freeze(
      FreezeRoot& root, const std::optional<T>& o, FreezePosition self) const {
    root.freezeField(self, issetField, o.has_value());
    if (o) {
      root.freezeField(self, valueField, o.value());
    }
  }

  void freeze(FreezeRoot& root, const T& o, FreezePosition self) const {
    root.freezeField(self, issetField, true);
    root.freezeField(self, valueField, o);
  }

  void thaw(ViewPosition self, std::optional<T>& out) const {
    bool set;
    thawField(self, issetField, set);
    if (set) {
      out.emplace();
      thawField(self, valueField, out.value());
    }
  }

  using View = OptionalFieldView<typename Layout<T>::View>;

  View view(ViewPosition self) const {
    View v;
    bool set;
    thawField(self, issetField, set);
    if (set) {
      v.emplace(valueField.layout.view(self(valueField.pos)));
    }
    return v;
  }

  void print(std::ostream& os, int level) const final {
    LayoutBase::print(os, level);
    os << "optional " << dwarfs::thrift_lite::demangle(type.name());
    issetField.print(os, level + 1);
    valueField.print(os, level + 1);
  }

  void clear() final {
    LayoutBase::clear();
    issetField.clear();
    valueField.clear();
  }

  FROZEN_SAVE_INLINE(FROZEN_SAVE_FIELD(isset) FROZEN_SAVE_FIELD(value))

  FROZEN_LOAD_INLINE(FROZEN_LOAD_FIELD(isset, 1) FROZEN_LOAD_FIELD(value, 2))
};
} // namespace detail

template <class T>
struct Layout<std::optional<T>>
    : public apache::thrift::frozen::detail::OptionalLayout<T> {};

} // namespace apache::thrift::frozen
