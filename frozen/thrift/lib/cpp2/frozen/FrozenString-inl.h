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

template <class T>
struct BufferHelpers {
  typedef typename T::value_type Item;
  static_assert(
      std::is_arithmetic<Item>::value || std::is_enum<Item>::value,
      "String storage requires simple item types");
  static size_t size(const T& src) { return src.size(); }
  static void copyTo(const T& src, std::span<Item> dst) {
    std::copy(src.begin(), src.end(), dst.begin());
  }
  static void thawTo(std::span<const Item> src, T& dst) {
    dst.assign(src.begin(), src.end());
  }
};

/**
 * for contiguous, blittable ranges
 */
template <class T>
struct StringLayout : public LayoutBase {
  typedef LayoutBase Base;
  typedef BufferHelpers<T> Helper;
  typedef typename Helper::Item Item;
  Field<size_t> distanceField;
  Field<size_t> countField;

  StringLayout()
      : LayoutBase(typeid(T)),
        distanceField(1, "distance"),
        countField(2, "count") {}

  FieldPosition maximize() {
    FieldPosition pos = startFieldPosition();
    FROZEN_MAXIMIZE_FIELD(distance);
    FROZEN_MAXIMIZE_FIELD(count);
    return pos;
  }

  FieldPosition layout(LayoutRoot& root, const T& o, LayoutPosition self) {
    FieldPosition pos = startFieldPosition();
    size_t n = Helper::size(o);
    if (!n) {
      return pos;
    }
    size_t dist =
        root.layoutBytesDistance(self.start, n * sizeof(Item), alignof(Item));
    pos = root.layoutField(self, pos, distanceField, dist);
    pos = root.layoutField(self, pos, countField, n);
    return pos;
  }

  void freeze(FreezeRoot& root, const T& o, FreezePosition self) const {
    size_t n = Helper::size(o);
    std::span<uint8_t> range;
    size_t dist;
    root.appendBytes(self.start, n * sizeof(Item), range, dist, alignof(Item));
    root.freezeField(self, distanceField, dist);
    root.freezeField(self, countField, n);
    std::span<Item> target(reinterpret_cast<Item*>(range.data()), n);
    Helper::copyTo(o, target);
  }

  void thaw(ViewPosition self, T& out) const {
    Helper::thawTo(view(self), out);
  }

  using View = std::conditional_t<
      std::is_same_v<Item, char>,
      std::string_view,
      std::span<const Item>>;

  View view(ViewPosition self) const {
    View range;
    size_t dist, n;
    thawField(self, countField, n);
    if (n) {
      thawField(self, distanceField, dist);
      const byte* read = self.start + dist;
      range = {reinterpret_cast<const Item*>(read), n};
    }
    return range;
  }

  void print(std::ostream& os, int level) const override {
    LayoutBase::print(os, level);
    os << "string of " << dwarfs::thrift_lite::demangle(type.name());
    distanceField.print(os, level + 1);
    countField.print(os, level + 1);
  }

  void clear() final {
    LayoutBase::clear();
    distanceField.clear();
    countField.clear();
  }

  FROZEN_SAVE_INLINE(FROZEN_SAVE_FIELD(distance) FROZEN_SAVE_FIELD(count))

  FROZEN_LOAD_INLINE(FROZEN_LOAD_FIELD(distance, 1) FROZEN_LOAD_FIELD(count, 2))

  static size_t hash(const View& v) {
    return XXH3_64bits(v.data(), sizeof(Item) * v.size());
  }
};

} // namespace detail

template <class T>
struct Layout<T, typename std::enable_if<IsString<T>::value>::type>
    : apache::thrift::frozen::detail::StringLayout<
          typename std::decay<T>::type> {};

} // namespace frozen
} // namespace thrift
} // namespace apache

THRIFT_DECLARE_TRAIT(IsString, std::string)
