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
 * Default serialization, which does little more than reinterpret_cast. Used for
 * POD structs and floats, but not pointers and integrals. Layout merely
 * specifies byte count required. Layout is simply the number of bytes needed.
 * Freezing/thawing is done by simply assigning to a reinterpreted pointer.
 */
template <class T>
struct TrivialLayout : public LayoutBase {
  using Base = LayoutBase;

  static constexpr bool kMayRequirePerItemInspection = false;

  TrivialLayout() = default;

  FieldPosition maximize() { return FieldPosition(sizeof(T), 0); }
  FieldPosition layout(LayoutRoot&, const T&, LayoutPosition /* start */) {
    return maximize();
  }

  void freeze(FreezeRoot&, const T& o, FreezePosition self) const {
    if (size == sizeof(T)) {
      std::memcpy(self.start, &o, sizeof(T));
      swap<T>(self.start);
    } else {
      throw LayoutException();
    }
  }

  void thaw(ViewPosition self, T& out) const {
    if (size == sizeof(T)) {
      std::memcpy(&out, self.start, sizeof(T));
      swap<T>(&out);
    } else {
      out = T{};
    }
  }

  std::string_view type_name() const final {
    return dwarfs::thrift_lite::type_name<T>;
  }

  void print(
      std::ostream& os,
      const LayoutPrintOptions& options = {},
      int level = 0) const override {
    LayoutBase::print(os, options, level);
    os << "blitted " << dwarfs::thrift_lite::type_name<T>;
  }

  void validate(LoadRoot& root) const override {
    Base::validate(root);
    if (!empty() && (size != sizeof(T) || bits != 0)) {
      throw schema::SchemaValidationException("invalid trivial layout");
    }
  }

  void inspectData(
      DataInspectionContext& context, DataPosition position) const override {
    Base::inspectData(context, position);
    if (!empty()) {
      context.requirePhysicalBytes(
          position.byteOffset, sizeof(T), "trivial value");
    }
  }

  using View = T;
  View view(ViewPosition self) const {
    View v;
    thaw(self, v);
    return v;
  }

  static size_t hash(const T& value) { return std::hash<T>()(value); }

 private:
  // Scalar arithmetic values frozen through TrivialLayout have a canonical
  // little-endian byte order on disk. Besides floating-point values, this
  // covers integral values explicitly frozen through TrivialLayout, most
  // notably the 64-bit hash-table block occupancy mask (BlockLayout), which
  // would otherwise be written in native byte order and misread when the
  // data is exchanged between hosts of different endianness. Other
  // trivially copyable types are stored in their native object
  // representation; see the PORTABILITY section of the format document.
  template <class O, class P>
  static void swap(P* ptr) {
    constexpr bool kIsSwappableScalar =
        (std::is_integral_v<O> || std::is_floating_point_v<O>) && sizeof(O) > 1;
    if constexpr (
        kIsSwappableScalar && std::endian::native == std::endian::big) {
      static_assert(
          std::is_same_v<O, P> || sizeof(P) == 1, "unexpected pointer type");
      using U = std::conditional_t<
          sizeof(O) == 2,
          uint16_t,
          std::conditional_t<sizeof(O) == 4, uint32_t, uint64_t>>;
      static_assert(sizeof(O) == sizeof(U), "unexpected scalar size");
      U tmp;
      std::memcpy(&tmp, ptr, sizeof(U));
      tmp = std::byteswap(tmp);
      std::memcpy(ptr, &tmp, sizeof(U));
    }
  }
};

template <class T>
struct IsBlitType
    : std::integral_constant<
          bool,
          (std::is_trivially_copyable_v<T> && !std::is_pointer_v<T> &&
           !std::is_enum_v<T> && !std::is_integral_v<T>)> {};

// std::pair<trivially copyable T1, trivially copyable T2> became
// trivially copyable too (first fixed in GCC 6.3) and conflicts with
// the PairLayout specialization.
template <class T>
struct IsStdPair : public std::false_type {};

template <class T1, class T2>
struct IsStdPair<std::pair<T1, T2>> : public std::true_type {};

template <class T>
struct IsStdOptional : public std::false_type {};

template <class T>
struct IsStdOptional<std::optional<T>> : public std::true_type {};

} // namespace detail

template <class T>
struct Layout<
    T,
    std::enable_if_t<
        apache::thrift::frozen::detail::IsBlitType<T>::value &&
        !apache::thrift::frozen::IsExcluded<T>::value &&
        !apache::thrift::frozen::detail::IsStdPair<T>::value &&
        !apache::thrift::frozen::detail::IsStdOptional<T>::value &&
        !std::is_pointer_v<T>>>
    : apache::thrift::frozen::detail::TrivialLayout<T> {};

namespace detail {

template <class T>
inline constexpr bool is_blit_layout_v =
    std::is_base_of_v<TrivialLayout<T>, Layout<T>>;

} // namespace detail

} // namespace apache::thrift::frozen
