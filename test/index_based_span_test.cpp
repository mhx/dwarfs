/* vim:set ts=2 sw=2 sts=2 et: */
/**
 * \author     Marcus Holland-Moritz (github@mhxnet.de)
 * \copyright  Copyright (c) Marcus Holland-Moritz
 *
 * This file is part of dwarfs.
 *
 * dwarfs is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * dwarfs is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with dwarfs.  If not, see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <algorithm>
#include <iterator>
#include <limits>
#include <ranges>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <dwarfs/container/index_based_span.h>

#include "packed_int_vector_test_helpers.h"

using namespace dwarfs::container;
using ::testing::ElementsAre;
using ::testing::ElementsAreArray;

namespace {

using scalar_vec = packed_int_vector<uint32_t>;
using auto_scalar_vec = auto_packed_int_vector<uint32_t>;
using compact_scalar_vec = compact_packed_int_vector<uint32_t>;
using compact_auto_scalar_vec = compact_auto_packed_int_vector<uint32_t>;
using segmented_scalar_vec = segmented_packed_int_vector<uint32_t, 4>;
using signed_scalar_vec = packed_int_vector<int32_t>;
using segmented_signed_vec = segmented_packed_int_vector<int32_t, 8>;
using tuple_vec = auto_packed_int_vector<std::tuple<uint16_t, uint32_t>>;

// ---------------------------------------------------------------------------
// static requirements
// ---------------------------------------------------------------------------

template <typename Vec>
void static_checks() {
  using span_type = index_based_span<Vec>;
  using const_span_type = index_based_const_span<Vec>;

  static_assert(detail::index_based_container<Vec>);

  // the span is a proper borrowed random-access view
  static_assert(std::ranges::random_access_range<span_type>);
  static_assert(std::ranges::random_access_range<const_span_type>);
  static_assert(std::ranges::sized_range<span_type>);
  static_assert(std::ranges::borrowed_range<span_type>);
  static_assert(std::ranges::borrowed_range<const_span_type>);
  static_assert(std::ranges::view<span_type>);

  // span iterators are the container's own iterators
  static_assert(
      std::same_as<typename span_type::iterator, typename Vec::iterator>);
  static_assert(std::same_as<typename const_span_type::iterator,
                             typename Vec::const_iterator>);
  static_assert(std::random_access_iterator<typename span_type::iterator>);
  static_assert(
      std::random_access_iterator<typename const_span_type::iterator>);

  // a const span object still gives mutable access to the elements
  static_assert(std::ranges::random_access_range<span_type const>);
  static_assert(std::same_as<std::ranges::iterator_t<span_type const>,
                             typename Vec::iterator>);

  // mutable spans convert to const spans, but not the other way round
  static_assert(std::convertible_to<span_type, const_span_type>);
  static_assert(!std::convertible_to<const_span_type, span_type>);

  // spans cannot be built from temporaries
  static_assert(std::is_constructible_v<span_type, Vec&>);
  static_assert(std::is_constructible_v<const_span_type, Vec const&>);
  static_assert(!std::is_constructible_v<span_type, Vec&&>);
  static_assert(!std::is_constructible_v<const_span_type, Vec&&>);

  // make_span picks the right constness
  static_assert(
      std::same_as<decltype(make_span(std::declval<Vec&>())), span_type>);
  static_assert(std::same_as<decltype(make_span(std::declval<Vec const&>())),
                             const_span_type>);
  static_assert(
      std::same_as<decltype(make_span(std::declval<Vec&>(), std::size_t{1})),
                   span_type>);
  static_assert(
      std::same_as<decltype(make_span(std::declval<Vec&>(), std::size_t{1},
                                      std::size_t{2})),
                   span_type>);
  static_assert(std::same_as<decltype(make_span(std::declval<Vec&>().begin(),
                                                std::declval<Vec&>().end())),
                             span_type>);
  static_assert(
      std::same_as<decltype(make_span(std::declval<Vec const&>().begin(),
                                      std::declval<Vec const&>().end())),
                   const_span_type>);
  static_assert(std::same_as<decltype(make_span(std::declval<Vec&>().begin(),
                                                std::size_t{2})),
                             span_type>);
}

template <typename Vec>
void static_checks_sortable() {
  static_assert(std::sortable<typename index_based_span<Vec>::iterator>);
}

template <typename Vec>
concept span_from_rvalue = requires(Vec v) { make_span(std::move(v)); };

template <typename Vec>
concept span_from_lvalue = requires(Vec& v) { make_span(v); };

template <typename Span>
concept has_set =
    requires(Span s) { s.set(std::size_t{0}, typename Span::value_type{}); };

template <typename Vec>
void static_checks_rvalue_and_mutability() {
  static_assert(span_from_lvalue<Vec>);
  static_assert(!span_from_rvalue<Vec>);
  static_assert(has_set<index_based_span<Vec>>);
  static_assert(!has_set<index_based_const_span<Vec>>);
}

TEST(index_based_span, static_requirements) {
  static_checks<scalar_vec>();
  static_checks<auto_scalar_vec>();
  static_checks<compact_scalar_vec>();
  static_checks<compact_auto_scalar_vec>();
  static_checks<segmented_scalar_vec>();
  static_checks<tuple_vec>();

  static_checks_sortable<scalar_vec>();
  static_checks_sortable<auto_scalar_vec>();
  static_checks_sortable<compact_scalar_vec>();
  static_checks_sortable<compact_auto_scalar_vec>();
  static_checks_sortable<segmented_scalar_vec>();

  static_checks_rvalue_and_mutability<scalar_vec>();
  static_checks_rvalue_and_mutability<compact_auto_scalar_vec>();
  static_checks_rvalue_and_mutability<segmented_scalar_vec>();
}

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

template <typename Vec>
constexpr bool has_bit_width_strategy = requires { Vec::auto_bit_width; };

template <typename Vec>
std::vector<typename Vec::value_type> test_values(std::size_t count) {
  std::vector<typename Vec::value_type> values;
  values.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    values.push_back(static_cast<typename Vec::value_type>(3 * i + 1));
  }
  return values;
}

/**
 * Build a vector holding `values`.
 *
 * Fixed-width vectors need a width that is wide enough for everything the
 * tests are going to write, so all test values must fit into `bits` bits.
 */
template <typename Vec>
Vec make_vec(std::vector<typename Vec::value_type> const& values,
             std::size_t bits = 8) {
  Vec vec;

  if constexpr (has_bit_width_strategy<Vec>) {
    vec.reset(bits, values.size());
  } else {
    vec.resize(values.size());
  }

  for (std::size_t i = 0; i < values.size(); ++i) {
    vec[i] = values[i];
  }

  return vec;
}

class span_type_names {
 public:
  template <typename Tag>
  static std::string GetName(int) {
    return Tag::name;
  }
};

} // namespace

/**
 * Short tags for the containers under test.
 *
 * gtest derives the `TypeParam` name it prints (and that ends up in the
 * `ctest` test name) from `typeid`, so a tag is the only way to keep those
 * names readable: with the container types used directly, every typed test is
 * suffixed with the fully expanded `basic_packed_int_vector<...>`.
 */
namespace span_test_types {

#define DWARFS_SPAN_TEST_TYPE(tag, vec)                                        \
  struct tag {                                                                 \
    using type = vec;                                                          \
    static constexpr char const* name = #tag;                                  \
  }

DWARFS_SPAN_TEST_TYPE(u32_fixed_heap, scalar_vec);
DWARFS_SPAN_TEST_TYPE(u32_auto_heap, auto_scalar_vec);
DWARFS_SPAN_TEST_TYPE(u32_fixed_compact, compact_scalar_vec);
DWARFS_SPAN_TEST_TYPE(u32_auto_compact, compact_auto_scalar_vec);
DWARFS_SPAN_TEST_TYPE(u32_segmented, segmented_scalar_vec);
DWARFS_SPAN_TEST_TYPE(i32_fixed_heap, signed_scalar_vec);
DWARFS_SPAN_TEST_TYPE(i32_segmented, segmented_signed_vec);

#undef DWARFS_SPAN_TEST_TYPE

} // namespace span_test_types

template <typename Tag>
class index_based_span_test : public ::testing::Test {};

using span_vector_types = ::testing::Types<
    span_test_types::u32_fixed_heap, span_test_types::u32_auto_heap,
    span_test_types::u32_fixed_compact, span_test_types::u32_auto_compact,
    span_test_types::u32_segmented, span_test_types::i32_fixed_heap,
    span_test_types::i32_segmented>;

TYPED_TEST_SUITE(index_based_span_test, span_vector_types, span_type_names);

TYPED_TEST(index_based_span_test, whole_span_covers_container) {
  using vec_type = typename TypeParam::type;

  auto const values = test_values<vec_type>(8);
  auto vec = make_vec<vec_type>(values);

  auto sp = make_span(vec);

  EXPECT_EQ(sp.size(), vec.size());
  EXPECT_FALSE(sp.empty());
  EXPECT_EQ(&sp.container(), &vec);
  EXPECT_THAT(sp.unpack(), ElementsAreArray(values));

  auto const csp = make_span(std::as_const(vec));

  static_assert(std::same_as<std::remove_const_t<decltype(csp)>,
                             index_based_const_span<vec_type>>);

  EXPECT_EQ(csp.size(), vec.size());
  EXPECT_THAT(csp, ElementsAreArray(values));
}

TYPED_TEST(index_based_span_test, subspan_first_last_and_nesting) {
  using vec_type = typename TypeParam::type;

  auto const values = test_values<vec_type>(8);
  auto vec = make_vec<vec_type>(values);

  auto const sp = make_span(vec, 2, 4);

  EXPECT_EQ(sp.size(), 4);
  EXPECT_THAT(sp.unpack(),
              ElementsAre(values[2], values[3], values[4], values[5]));

  EXPECT_THAT(sp.first(2).unpack(), ElementsAre(values[2], values[3]));
  EXPECT_THAT(sp.last(2).unpack(), ElementsAre(values[4], values[5]));
  EXPECT_THAT(sp.subspan(1).unpack(),
              ElementsAre(values[3], values[4], values[5]));
  EXPECT_THAT(sp.subspan(1, 2).unpack(), ElementsAre(values[3], values[4]));
  EXPECT_THAT(sp.subspan(1).subspan(1, 1).unpack(), ElementsAre(values[4]));

  EXPECT_TRUE(sp.subspan(4).empty());
  EXPECT_TRUE(sp.subspan(2, 0).empty());
  EXPECT_TRUE(sp.first(0).empty());
  EXPECT_TRUE(sp.last(0).empty());

  // offset-only span reaches to the end of the container
  auto const tail = make_span(vec, 5);
  EXPECT_THAT(tail.unpack(), ElementsAre(values[5], values[6], values[7]));
}

TYPED_TEST(index_based_span_test, element_access) {
  using vec_type = typename TypeParam::type;

  auto const values = test_values<vec_type>(8);
  auto vec = make_vec<vec_type>(values);

  auto const sp = make_span(vec, 2, 4);

  EXPECT_EQ(sp.get(0), values[2]);
  EXPECT_EQ(sp.get(3), values[5]);
  EXPECT_EQ(sp[1], values[3]);
  EXPECT_EQ(sp.at(1), values[3]);
  EXPECT_EQ(sp.front(), values[2]);
  EXPECT_EQ(sp.back(), values[5]);

  EXPECT_THROW(sp.at(4), std::out_of_range);
  EXPECT_THROW(sp.at(4) = 1, std::out_of_range);

  auto const csp = make_span(std::as_const(vec), 2, 4);

  EXPECT_EQ(csp.get(0), values[2]);
  EXPECT_EQ(csp[1], values[3]);
  EXPECT_EQ(csp.at(3), values[5]);
  EXPECT_EQ(csp.front(), values[2]);
  EXPECT_EQ(csp.back(), values[5]);
  EXPECT_THROW(csp.at(4), std::out_of_range);
}

TYPED_TEST(index_based_span_test, writes_are_visible_in_container) {
  using vec_type = typename TypeParam::type;
  using value_type = typename vec_type::value_type;

  auto const values = test_values<vec_type>(8);
  auto vec = make_vec<vec_type>(values);

  // note: const span object, mutable elements
  auto const sp = make_span(vec, 2, 4);

  sp[0] = value_type{100};
  sp.set(1, value_type{101});
  sp.at(2) = value_type{102};
  sp.back() = value_type{103};

  EXPECT_THAT(vec.unpack(), ElementsAre(values[0], values[1], 100, 101, 102,
                                        103, values[6], values[7]));

  // and the other way round
  vec[3] = value_type{7};
  EXPECT_EQ(sp.get(1), 7);
}

TYPED_TEST(index_based_span_test, iterators_are_container_iterators) {
  using vec_type = typename TypeParam::type;

  auto const values = test_values<vec_type>(8);
  auto vec = make_vec<vec_type>(values);

  auto const sp = make_span(vec, 2, 4);

  EXPECT_TRUE(sp.begin() == vec.begin() + 2);
  EXPECT_TRUE(sp.end() == vec.begin() + 6);
  EXPECT_EQ(sp.end() - sp.begin(), 4);
  EXPECT_EQ(std::ranges::distance(sp), 4);

  EXPECT_TRUE(sp.cbegin() == std::as_const(vec).begin() + 2);
  EXPECT_TRUE(sp.cend() == std::as_const(vec).begin() + 6);

  EXPECT_TRUE(std::ranges::equal(
      sp, std::vector(values.begin() + 2, values.begin() + 6)));

  std::vector<typename vec_type::value_type> reversed;
  std::ranges::copy(sp.rbegin(), sp.rend(), std::back_inserter(reversed));
  EXPECT_THAT(reversed,
              ElementsAre(values[5], values[4], values[3], values[2]));

  // round-trip through iterators
  auto const from_iterators = make_span(sp.begin(), sp.end());
  static_assert(std::same_as<std::remove_const_t<decltype(from_iterators)>,
                             index_based_span<vec_type>>);
  EXPECT_EQ(from_iterators.size(), sp.size());
  EXPECT_THAT(from_iterators.unpack(), ElementsAreArray(sp.unpack()));

  auto const from_count = make_span(vec.begin() + 2, std::size_t{4});
  EXPECT_THAT(from_count.unpack(), ElementsAreArray(sp.unpack()));

  auto const from_const_iterators =
      make_span(std::as_const(vec).begin() + 2, std::as_const(vec).end());
  static_assert(
      std::same_as<std::remove_const_t<decltype(from_const_iterators)>,
                   index_based_const_span<vec_type>>);
  EXPECT_EQ(from_const_iterators.size(), 6);
}

TYPED_TEST(index_based_span_test, converts_to_const_span) {
  using vec_type = typename TypeParam::type;

  auto const values = test_values<vec_type>(8);
  auto vec = make_vec<vec_type>(values);

  auto const sp = make_span(vec, 2, 4);
  index_based_const_span<vec_type> csp = sp;

  EXPECT_EQ(csp.size(), sp.size());
  EXPECT_EQ(&csp.container(), &vec);
  EXPECT_THAT(csp.unpack(), ElementsAreArray(sp.unpack()));

  // conversion also works through a function parameter
  auto const consume = [&values](index_based_const_span<vec_type> s) {
    EXPECT_THAT(s.unpack(),
                ElementsAre(values[2], values[3], values[4], values[5]));
  };

  consume(sp);
}

TYPED_TEST(index_based_span_test, ranges_algorithms_operate_in_place) {
  using vec_type = typename TypeParam::type;
  using value_type = typename vec_type::value_type;

  auto vec = make_vec<vec_type>({5, 4, 3, 2, 1, 0, 9, 8});

  auto const sp = make_span(vec, 1, 4);

  std::ranges::sort(sp);

  EXPECT_THAT(vec.unpack(), ElementsAre(5, 1, 2, 3, 4, 0, 9, 8));

  std::ranges::fill(sp, value_type{6});

  EXPECT_THAT(vec.unpack(), ElementsAre(5, 6, 6, 6, 6, 0, 9, 8));

  EXPECT_EQ(std::ranges::count(make_span(vec), value_type{6}), 4);
  EXPECT_TRUE(std::ranges::find(sp, value_type{6}) == sp.begin());

  std::vector<value_type> const source{10, 11, 12, 13};
  std::ranges::copy(source, sp.begin());

  EXPECT_THAT(vec.unpack(), ElementsAre(5, 10, 11, 12, 13, 0, 9, 8));

  // pipe a span into a view adaptor
  auto const doubled = make_span(vec, 1, 4) | std::views::transform([](auto v) {
                         return static_cast<value_type>(2 * v);
                       });

  EXPECT_THAT(std::vector(doubled.begin(), doubled.end()),
              ElementsAre(20, 22, 24, 26));
}

TYPED_TEST(index_based_span_test, empty_and_default_constructed_spans) {
  using vec_type = typename TypeParam::type;

  vec_type empty_vec;
  auto const empty_span = make_span(empty_vec);

  EXPECT_TRUE(empty_span.empty());
  EXPECT_EQ(empty_span.size(), 0);
  EXPECT_TRUE(empty_span.begin() == empty_span.end());
  EXPECT_THAT(empty_span.unpack(), ElementsAre());
  EXPECT_THROW(empty_span.at(0), std::out_of_range);

  index_based_span<vec_type> default_span;

  EXPECT_TRUE(default_span.empty());
  EXPECT_EQ(default_span.size(), 0);
  EXPECT_TRUE(default_span.begin() == default_span.end());
  EXPECT_EQ(std::ranges::distance(default_span), 0);
  EXPECT_TRUE(default_span.subspan(0).empty());
  EXPECT_THROW(default_span.at(0), std::out_of_range);

  auto const values = test_values<vec_type>(4);
  auto vec = make_vec<vec_type>(values);

  // an empty span in the middle of a non-empty container
  auto const middle = make_span(vec, 2, 0);

  EXPECT_TRUE(middle.empty());
  EXPECT_TRUE(middle.begin() == middle.end());
  EXPECT_TRUE(middle.begin() == vec.begin() + 2);
}

// ---------------------------------------------------------------------------
// non-typed tests for behaviour specific to individual container types
// ---------------------------------------------------------------------------

TEST(index_based_span, survives_bit_width_growth_of_heap_vector) {
  auto_scalar_vec vec;
  vec.resize(8);

  auto const sp = make_span(vec, 2, 4);

  ASSERT_EQ(vec.bits(), 0);

  sp[0] = 5;
  EXPECT_EQ(vec.bits(), 3);

  // this repacks the whole vector into a wider representation
  sp[3] = std::numeric_limits<uint32_t>::max();

  EXPECT_EQ(vec.bits(), 32);
  EXPECT_EQ(sp.size(), 4);
  EXPECT_EQ(sp.get(0), 5);
  EXPECT_EQ(sp.get(3), std::numeric_limits<uint32_t>::max());
  EXPECT_THAT(sp.unpack(),
              ElementsAre(5, 0, 0, std::numeric_limits<uint32_t>::max()));
  EXPECT_THAT(
      vec.unpack(),
      ElementsAre(0, 0, 5, 0, 0, std::numeric_limits<uint32_t>::max(), 0, 0));
  EXPECT_TRUE(sp.begin() == vec.begin() + 2);
}

TEST(index_based_span, survives_inline_to_heap_transition) {
  compact_auto_scalar_vec vec;
  vec.resize(8);

  // the test only makes sense if 8 one-bit elements still fit inline
  ASSERT_GE(compact_auto_scalar_vec::inline_capacity_for_bits(1),
            std::size_t{8});
  ASSERT_TRUE(vec.is_inline());

  auto const sp = make_span(vec, 4, 4);

  sp[0] = 1;
  ASSERT_TRUE(vec.is_inline());

  // forces the payload out of the object itself and onto the heap
  sp[1] = std::numeric_limits<uint32_t>::max();

  EXPECT_FALSE(vec.is_inline());
  EXPECT_TRUE(vec.uses_heap());
  EXPECT_THAT(sp.unpack(),
              ElementsAre(1, std::numeric_limits<uint32_t>::max(), 0, 0));
  EXPECT_THAT(
      vec.unpack(),
      ElementsAre(0, 0, 0, 0, 1, std::numeric_limits<uint32_t>::max(), 0, 0));
}

TEST(index_based_span, survives_segment_local_widening) {
  segmented_scalar_vec vec;
  vec.resize(12);

  // spans the second and third segment
  auto const sp = make_span(vec, 4, 8);

  ASSERT_EQ(vec.segment_count(), 3);

  sp[1] = 31;
  sp[6] = 3;

  auto const hist = vec.segment_bits_histogram();
  EXPECT_EQ(hist[0], 1);
  EXPECT_EQ(hist[2], 1);
  EXPECT_EQ(hist[5], 1);

  EXPECT_THAT(sp.unpack(), ElementsAre(0, 31, 0, 0, 0, 0, 3, 0));
  EXPECT_THAT(vec.unpack(), ElementsAre(0, 0, 0, 0, 0, 31, 0, 0, 0, 0, 3, 0));

  vec.optimize_storage();

  EXPECT_THAT(sp.unpack(), ElementsAre(0, 31, 0, 0, 0, 0, 3, 0));
}

TEST(index_based_span, span_iterators_can_be_passed_to_erase) {
  scalar_vec vec(8, 8);

  for (uint32_t i = 0; i < 8; ++i) {
    vec[i] = i;
  }

  auto const sp = make_span(vec, 2, 3);

  auto const pos = vec.erase(sp.begin(), sp.end());

  EXPECT_EQ(pos.get_index(), 2);
  EXPECT_THAT(vec.unpack(), ElementsAre(0, 1, 5, 6, 7));
}

TEST(index_based_span, tuple_fields_are_accessible_through_the_proxy) {
  tuple_vec vec;
  vec.push_back({1, 10});
  vec.push_back({2, 20});
  vec.push_back({3, 30});
  vec.push_back({4, 40});

  auto const sp = make_span(vec, 1, 2);

  EXPECT_EQ(get<0>(sp[0]), 2);
  EXPECT_EQ(get<1>(sp[1]), 30);

  get<0>(sp[0]) = 7;
  get<1>(sp[1]) = 70;

  EXPECT_EQ(sp.get(0), (std::tuple<uint16_t, uint32_t>{7, 20}));
  EXPECT_EQ(sp.get(1), (std::tuple<uint16_t, uint32_t>{3, 70}));
  EXPECT_THAT(vec.unpack(),
              ElementsAre(std::make_tuple(1, 10), std::make_tuple(7, 20),
                          std::make_tuple(3, 70), std::make_tuple(4, 40)));

  auto const csp = make_span(std::as_const(vec), 1, 2);

  EXPECT_EQ(std::get<0>(csp[0]), 7);
  EXPECT_EQ(std::get<1>(csp[1]), 70);
}

TEST(index_based_span, spans_over_different_container_types_are_distinct) {
  static_assert(!std::same_as<index_based_span<scalar_vec>,
                              index_based_span<auto_scalar_vec>>);
  static_assert(
      !std::is_constructible_v<index_based_span<scalar_vec>, auto_scalar_vec&>);
  static_assert(!std::is_constructible_v<index_based_span<scalar_vec>,
                                         index_based_span<compact_scalar_vec>>);
}
