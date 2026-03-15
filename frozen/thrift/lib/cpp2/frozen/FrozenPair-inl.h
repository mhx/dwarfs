/*
 * SPDX-FileCopyrightText: Copyright (c) Meta Platforms, Inc. and affiliates.
 * SPDX-FileCopyrightText: Copyright (c) Marcus Holland-Moritz
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * This file is derived from fbthrift and has been modified by
 * Marcus Holland-Moritz for use in dwarfs.
 */

namespace apache {
namespace thrift {
namespace frozen {

namespace detail {

/**
 * Layout specialization a pair of values, layed out as simple fields.
 */
template <class First, class Second>
struct PairLayout : public LayoutBase {
  typedef LayoutBase Base;
  typedef std::pair<First, Second> T;
  typedef PairLayout LayoutSelf;
  typedef typename std::decay<First>::type FirstDecayed;
  typedef typename std::decay<Second>::type SecondDecayed;
  Field<FirstDecayed> firstField;
  Field<SecondDecayed> secondField;

  PairLayout()
      : LayoutBase(typeid(T)),
        firstField(1, "first"),
        secondField(2, "second") {}

  FieldPosition maximize() {
    FieldPosition pos = startFieldPosition();
    FROZEN_MAXIMIZE_FIELD(first);
    FROZEN_MAXIMIZE_FIELD(second);
    return pos;
  }

  FieldPosition layout(
      LayoutRoot& root,
      const std::pair<First, Second>& o,
      LayoutPosition self) {
    FieldPosition pos = startFieldPosition();
    pos = root.layoutField(self, pos, firstField, o.first);
    pos = root.layoutField(self, pos, secondField, o.second);
    return pos;
  }

  void freeze(
      FreezeRoot& root,
      const std::pair<First, Second>& o,
      FreezePosition self) const {
    root.freezeField(self, firstField, o.first);
    root.freezeField(self, secondField, o.second);
  }

  void thaw(ViewPosition self, std::pair<First, Second>& out) const {
    thawField(self, firstField, const_cast<FirstDecayed&>(out.first));
    thawField(self, secondField, const_cast<SecondDecayed&>(out.second));
  }

  FROZEN_VIEW(FROZEN_VIEW_FIELD(first, FirstDecayed)
                  FROZEN_VIEW_FIELD(second, SecondDecayed))

  void print(std::ostream& os, int level) const final {
    LayoutBase::print(os, level);
    os << dwarfs::thrift_lite::demangle(type.name());
    firstField.print(os, level + 1);
    secondField.print(os, level + 1);
  }

  void clear() final {
    LayoutBase::clear();
    firstField.clear();
    secondField.clear();
  }

  FROZEN_SAVE_INLINE(FROZEN_SAVE_FIELD(first) FROZEN_SAVE_FIELD(second))

  FROZEN_LOAD_INLINE(FROZEN_LOAD_FIELD(first, 1) FROZEN_LOAD_FIELD(second, 2))
};

} // namespace detail

template <class First, class Second>
struct Layout<std::pair<First, Second>>
    : public apache::thrift::frozen::detail::PairLayout<First, Second> {};

} // namespace frozen
} // namespace thrift
} // namespace apache
