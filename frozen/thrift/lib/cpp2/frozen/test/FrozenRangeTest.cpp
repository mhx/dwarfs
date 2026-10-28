/*
 * SPDX-FileCopyrightText: Copyright (c) Meta Platforms, Inc. and affiliates.
 * SPDX-FileCopyrightText: Copyright (c) Marcus Holland-Moritz
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * This file is derived from fbthrift and has been modified by
 * Marcus Holland-Moritz for use in dwarfs.
 */

#include <numeric>

#include <gtest/gtest.h>

#include <thrift/lib/cpp2/frozen/FrozenUtil.h>

namespace {

struct CountedInline {
  uint32_t value = 0;
  ~CountedInline() {}
};

struct CountedFallback {
  uint32_t value = 0;
  ~CountedFallback() {}
};

template <class T>
struct CountingLayout
    : apache::thrift::frozen::detail::PackedIntegerLayout<uint32_t> {
  using Base = apache::thrift::frozen::detail::PackedIntegerLayout<uint32_t>;
  using View = uint32_t;

  // https://github.com/llvm/llvm-project/issues/231652
  [[maybe_unused]] inline static size_t calls = 0;

  apache::thrift::frozen::FieldPosition layout(
      apache::thrift::frozen::LayoutRoot& root,
      const T& x,
      apache::thrift::frozen::LayoutPosition self) {
    ++calls;
    return Base::layout(root, x.value, self);
  }

  void freeze(
      apache::thrift::frozen::FreezeRoot& root,
      const T& x,
      apache::thrift::frozen::FreezePosition self) const {
    Base::freeze(root, x.value, self);
  }

  void thaw(apache::thrift::frozen::ViewPosition self, T& out) const {
    Base::thaw(self, out.value);
  }

  View view(apache::thrift::frozen::ViewPosition self) const {
    View out{};
    Base::thaw(self, out);
    return out;
  }
};

} // namespace

namespace apache::thrift::frozen {

template <>
struct Layout<CountedInline> : CountingLayout<CountedInline> {
  static constexpr bool kCanReuseItemLayoutAfterFirstPass = true;
};

template <>
struct Layout<CountedFallback> : CountingLayout<CountedFallback> {};

} // namespace apache::thrift::frozen

namespace apache::thrift {
using namespace frozen;

TEST(FrozenRangeLayoutReuse, OnlyExplicitlySafeLayoutsQualify) {
  using detail::can_reuse_item_layout_v;
  static_assert(can_reuse_item_layout_v<Layout<int32_t>>);
  static_assert(can_reuse_item_layout_v<Layout<bool>>);
  static_assert(can_reuse_item_layout_v<Layout<CountedInline>>);
  static_assert(!can_reuse_item_layout_v<Layout<CountedFallback>>);
  static_assert(!can_reuse_item_layout_v<Layout<std::optional<int32_t>>>);
  static_assert(!can_reuse_item_layout_v<Layout<std::vector<int32_t>>>);
  static_assert(!can_reuse_item_layout_v<Layout<std::string>>);
}

TEST(FrozenRangeLayoutReuse, OncePerArrayAndPerLayoutInvocation) {
  std::vector<CountedInline> values(129);
  for (size_t i = 0; i < values.size(); ++i) {
    values[i].value = i == 127 ? (1 << 27) : static_cast<uint32_t>(i);
  }

  Layout<std::vector<CountedInline>> layout;
  Layout<CountedInline>::calls = 0;
  const auto size = LayoutRoot::layout(values, layout);
  EXPECT_EQ(values.size(), Layout<CountedInline>::calls);
  EXPECT_EQ(28, layout.itemField.layout.bits);

  // Both calls must scan their input on the first pass.
  values.back().value = 0x80000000;
  Layout<CountedInline>::calls = 0;
  const auto largerSize = LayoutRoot::layout(values, layout);
  EXPECT_EQ(values.size(), Layout<CountedInline>::calls);
  EXPECT_EQ(32, layout.itemField.layout.bits);
  EXPECT_GE(largerSize, size);

  // Non-opted-in layouts inspect each element on every pass.
  std::vector<CountedFallback> fallback(values.size());
  for (size_t i = 0; i < values.size(); ++i) {
    fallback[i].value = values[i].value;
  }
  Layout<CountedFallback>::calls = 0;
  Layout<std::vector<CountedFallback>> fallbackLayout;
  EXPECT_EQ(largerSize, LayoutRoot::layout(fallback, fallbackLayout));
  EXPECT_GT(Layout<CountedFallback>::calls, fallback.size());
  EXPECT_EQ(layout.itemField.layout.bits, fallbackLayout.itemField.layout.bits);
  EXPECT_EQ(
      freezeDataToString(values, layout),
      freezeDataToString(fallback, fallbackLayout));
}

template <class T>
class FrozenRange : public ::testing::Test {};
TYPED_TEST_SUITE_P(FrozenRange);

TYPED_TEST_P(FrozenRange, ArrayLike) {
  TypeParam v{10, 11, 12, 13};
  auto fv = freeze(v);
  EXPECT_EQ(fv.size(), 4);
  EXPECT_FALSE(fv.empty());
  EXPECT_EQ(fv[2], 12);
  EXPECT_EQ(fv.at(2), 12);
  EXPECT_THROW(fv.at(4), std::out_of_range);
}

TYPED_TEST_P(FrozenRange, Iterators) {
  TypeParam v{10, 11, 12, 13};
  auto fv = freeze(v);
  auto it = fv.begin();
  EXPECT_EQ(*(it + 2), 12);
  EXPECT_EQ(*it, 10);
  EXPECT_EQ(*it++, 10);
  EXPECT_EQ(*it++, 11);
  EXPECT_EQ(*it, 12);
  EXPECT_EQ(*(it - 2), 10);
  EXPECT_EQ(*++it, 13);
  it = fv.end();
  EXPECT_EQ(*--it, 13);
  EXPECT_EQ(*it--, 13);
  EXPECT_EQ(*it--, 12);
  EXPECT_EQ(*--it, 10);
  std::adjacent_difference(fv.begin(), fv.end(), v.begin());
  TypeParam ref{10, 1, 1, 1};
  EXPECT_EQ(v, ref);
}

TYPED_TEST_P(FrozenRange, IteratorComparisons) {
  TypeParam v{10, 11, 12, 13};
  auto fv = freeze(v);
  auto a = fv.begin();
  auto b = a + 1;
  auto c = b + 1;

  // ==
  EXPECT_EQ(1, a == a);
  EXPECT_EQ(0, a == b);
  EXPECT_EQ(0, a == c);
  EXPECT_EQ(0, b == a);
  EXPECT_EQ(1, b == b);
  EXPECT_EQ(0, b == c);
  EXPECT_EQ(0, c == a);
  EXPECT_EQ(0, c == b);
  EXPECT_EQ(1, c == c);

  // !=
  EXPECT_EQ(0, a != a);
  EXPECT_EQ(1, a != b);
  EXPECT_EQ(1, a != c);
  EXPECT_EQ(1, b != a);
  EXPECT_EQ(0, b != b);
  EXPECT_EQ(1, b != c);
  EXPECT_EQ(1, c != a);
  EXPECT_EQ(1, c != b);
  EXPECT_EQ(0, c != c);

  // <
  EXPECT_EQ(0, a < a);
  EXPECT_EQ(1, a < b);
  EXPECT_EQ(1, a < c);
  EXPECT_EQ(0, b < a);
  EXPECT_EQ(0, b < b);
  EXPECT_EQ(1, b < c);
  EXPECT_EQ(0, c < a);
  EXPECT_EQ(0, c < b);
  EXPECT_EQ(0, c < c);

  // <=
  EXPECT_EQ(1, a <= a);
  EXPECT_EQ(1, a <= b);
  EXPECT_EQ(1, a <= c);
  EXPECT_EQ(0, b <= a);
  EXPECT_EQ(1, b <= b);
  EXPECT_EQ(1, b <= c);
  EXPECT_EQ(0, c <= a);
  EXPECT_EQ(0, c <= b);
  EXPECT_EQ(1, c <= c);

  // >
  EXPECT_EQ(0, a > a);
  EXPECT_EQ(0, a > b);
  EXPECT_EQ(0, a > c);
  EXPECT_EQ(1, b > a);
  EXPECT_EQ(0, b > b);
  EXPECT_EQ(0, b > c);
  EXPECT_EQ(1, c > a);
  EXPECT_EQ(1, c > b);
  EXPECT_EQ(0, c > c);

  // >=
  EXPECT_EQ(1, a >= a);
  EXPECT_EQ(0, a >= b);
  EXPECT_EQ(0, a >= c);
  EXPECT_EQ(1, b >= a);
  EXPECT_EQ(1, b >= b);
  EXPECT_EQ(0, b >= c);
  EXPECT_EQ(1, c >= a);
  EXPECT_EQ(1, c >= b);
  EXPECT_EQ(1, c >= c);
}

TYPED_TEST_P(FrozenRange, Zeros) {
  TypeParam v(1000, 0);
  // If all the values are zero-sized, we only need space to store the count.
  EXPECT_EQ(frozenSize(v), 2);
  // only a single byte for 200 empty strings
  EXPECT_EQ(frozenSize(std::vector<std::string>(200)), 1);
  auto fv = freeze(v);
  EXPECT_EQ(fv.size(), 1000);
  auto it = fv.begin();
  EXPECT_EQ(*it++, 0);
  EXPECT_EQ(*it++, 0);
  size_t n = 0;
  for (const auto& item : fv) {
    (void)item;
    ++n; // ensure iteration still works despite zero-byte items
  }
  EXPECT_EQ(n, 1000);
}

REGISTER_TYPED_TEST_SUITE_P(
    FrozenRange, ArrayLike, Iterators, IteratorComparisons, Zeros);
using MyTypes = ::testing::Types<std::vector<int>, std::deque<int>>;
INSTANTIATE_TYPED_TEST_SUITE_P(Ranges, FrozenRange, MyTypes);

template <class T>
class FrozenRangeNested : public ::testing::Test {};
TYPED_TEST_SUITE_P(FrozenRangeNested);

TYPED_TEST_P(FrozenRangeNested, VectorVectorInt) {
  TypeParam vvi{{2, 3, 5, 7}, {11, 13, 17, 19}};
  auto fvvi = freeze(vvi);
  auto tvvi = fvvi.thaw();
  EXPECT_EQ(tvvi, vvi);
}

TYPED_TEST_P(FrozenRangeNested, FrontBack) {
  TypeParam vvi{{2, 3, 5, 7}, {11}};
  auto fvvi = freeze(vvi);
  EXPECT_EQ(2, fvvi.front().front());
  EXPECT_EQ(7, fvvi.front().back());
  EXPECT_EQ(11, fvvi.back().front());
  EXPECT_EQ(11, fvvi.back().back());
}

REGISTER_TYPED_TEST_SUITE_P(FrozenRangeNested, VectorVectorInt, FrontBack);
using MyTypesNested = ::testing::Types<
    std::vector<std::vector<int>>,
    std::vector<std::deque<int>>,
    std::deque<std::deque<int>>,
    std::deque<std::vector<int>>>;
INSTANTIATE_TYPED_TEST_SUITE_P(RangesNested, FrozenRangeNested, MyTypesNested);

} // namespace apache::thrift
