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

#ifdef __clang__
#pragma clang optimize off
#endif

#include "packed_int_vector_test_helpers.h"

using namespace dwarfs::test;
using namespace dwarfs::internal;
using dwarfs::from_range;
using ::testing::ElementsAre;
using ::testing::ElementsAreArray;

namespace {

template <typename T>
class single_pass_input_iterator {
 public:
  using iterator_concept = std::input_iterator_tag;
  using iterator_category = std::input_iterator_tag;
  using value_type = T;
  using difference_type = std::ptrdiff_t;
  using reference = T;

  single_pass_input_iterator() = default;
  single_pass_input_iterator(std::vector<T> const* values, std::size_t index)
      : values_{values}
      , index_{index} {}

  reference operator*() const { return (*values_)[index_]; }

  single_pass_input_iterator& operator++() {
    ++index_;
    return *this;
  }

  void operator++(int) { ++index_; }

  friend bool
  operator==(single_pass_input_iterator a, single_pass_input_iterator b) {
    return a.values_ == b.values_ && a.index_ == b.index_;
  }

 private:
  std::vector<T> const* values_{nullptr};
  std::size_t index_{0};
};

template <typename T>
struct single_pass_input_range {
  std::vector<T> values;

  auto begin() { return single_pass_input_iterator<T>{&values, 0}; }
  auto end() { return single_pass_input_iterator<T>{&values, values.size()}; }
};

} // namespace

template <typename Vec>
class packed_int_vec_test : public ::testing::Test {};

using packed_vector_type_types =
    ::testing::Types<packed_int_vector_type_selector<uint8_t>,
                     compact_packed_int_vector_type_selector<int8_t>,
                     packed_int_vector_type_selector<uint16_t>,
                     compact_packed_int_vector_type_selector<int32_t>,
                     compact_packed_int_vector_type_selector<uint64_t>,
                     packed_int_vector_type_selector<int64_t>>;

TYPED_TEST_SUITE(packed_int_vec_test, packed_vector_type_types);

TYPED_TEST(packed_int_vec_test, value_proxy_can_assign_from_another_proxy) {
  using vec_type = typename TypeParam::auto_type;

  vec_type vec;

  vec.push_back(1);
  vec.push_back(2);
  vec.push_back(3);

  vec[0] = vec[2];

  EXPECT_THAT(vec, ElementsAre(3, 2, 3));
}

TYPED_TEST(packed_int_vec_test, value_proxy_is_swappable) {
  using vec_type = typename TypeParam::auto_type;

  vec_type vec;

  vec.push_back(1);
  vec.push_back(2);
  vec.push_back(3);

  using std::swap;
  swap(vec[0], vec[2]);

  EXPECT_THAT(vec, ElementsAre(3, 2, 1));
}

TYPED_TEST(packed_int_vec_test,
           value_proxy_swap_works_across_block_boundaries) {
  using vec_type = typename TypeParam::auto_type;

  vec_type vec;

  for (int i = 0; i < 8; ++i) {
    vec.push_back(i + 1);
  }

  using std::swap;
  swap(vec[1], vec[6]);

  EXPECT_EQ(vec[0], 1);
  EXPECT_EQ(vec[1], 7);
  EXPECT_EQ(vec[6], 2);
  EXPECT_EQ(vec[7], 8);
}

TYPED_TEST(packed_int_vec_test, iterator_concepts_static_asserts) {
  using fixed_vec = typename TypeParam::type;
  using auto_vec = typename TypeParam::auto_type;

  static_assert(std::random_access_iterator<typename fixed_vec::iterator>);
  static_assert(
      std::random_access_iterator<typename fixed_vec::const_iterator>);
  static_assert(std::ranges::random_access_range<fixed_vec>);
  static_assert(std::ranges::random_access_range<fixed_vec const>);
  static_assert(std::ranges::common_range<fixed_vec>);
  static_assert(std::ranges::common_range<fixed_vec const>);
  static_assert(std::ranges::sized_range<fixed_vec>);
  static_assert(std::ranges::sized_range<fixed_vec const>);
  static_assert(std::indirectly_readable<typename fixed_vec::iterator>);
  static_assert(std::indirectly_readable<typename fixed_vec::const_iterator>);
  static_assert(std::indirectly_writable<typename fixed_vec::iterator,
                                         typename fixed_vec::value_type>);
  static_assert(std::indirectly_swappable<typename fixed_vec::iterator,
                                          typename fixed_vec::iterator>);
  static_assert(std::sortable<typename fixed_vec::iterator>);

  static_assert(std::random_access_iterator<typename auto_vec::iterator>);
  static_assert(std::random_access_iterator<typename auto_vec::const_iterator>);
  static_assert(std::ranges::random_access_range<auto_vec>);
  static_assert(std::ranges::random_access_range<auto_vec const>);
  static_assert(std::ranges::common_range<auto_vec>);
  static_assert(std::ranges::common_range<auto_vec const>);
  static_assert(std::ranges::sized_range<auto_vec>);
  static_assert(std::ranges::sized_range<auto_vec const>);
  static_assert(std::indirectly_readable<typename auto_vec::iterator>);
  static_assert(std::indirectly_readable<typename auto_vec::const_iterator>);
  static_assert(std::indirectly_writable<typename auto_vec::iterator,
                                         typename auto_vec::value_type>);
  static_assert(std::indirectly_swappable<typename auto_vec::iterator,
                                          typename auto_vec::iterator>);
  static_assert(std::sortable<typename auto_vec::iterator>);
}

TYPED_TEST(packed_int_vec_test, iterator_basic_traversal_and_arithmetic) {
  using vec_type = typename TypeParam::auto_type;

  vec_type vec;
  for (int i = 0; i < 6; ++i) {
    vec.push_back(i + 1);
  }

  auto it = vec.begin();
  auto end = vec.end();

  EXPECT_EQ(end - it, 6);
  EXPECT_EQ(it - it, 0);

  EXPECT_EQ(*it, 1);
  EXPECT_EQ(it[0], 1);
  EXPECT_EQ(it[3], 4);

  ++it;
  EXPECT_EQ(*it, 2);

  it++;
  EXPECT_EQ(*it, 3);

  --it;
  EXPECT_EQ(*it, 2);

  it += 3;
  EXPECT_EQ(*it, 5);

  it -= 2;
  EXPECT_EQ(*it, 3);

  auto it2 = vec.begin() + 4;
  EXPECT_EQ(*it2, 5);
  EXPECT_EQ(it2 - vec.begin(), 4);
  EXPECT_TRUE(vec.begin() < it2);
  EXPECT_TRUE(it2 > vec.begin());
  EXPECT_TRUE(vec.begin() <= vec.begin());
  EXPECT_TRUE(it2 >= vec.begin());
}

TYPED_TEST(packed_int_vec_test, const_iterator_basic_traversal_and_arithmetic) {
  using vec_type = typename TypeParam::auto_type;

  vec_type vec;
  for (int i = 0; i < 6; ++i) {
    vec.push_back(i + 1);
  }

  vec_type const& cvec = vec;

  auto it = cvec.begin();
  auto end = cvec.end();

  EXPECT_EQ(end - it, 6);
  EXPECT_EQ(*it, 1);
  EXPECT_EQ(it[4], 5);

  ++it;
  EXPECT_EQ(*it, 2);

  it += 3;
  EXPECT_EQ(*it, 5);

  auto cit = cvec.cbegin();
  auto cit2 = cit + 2;
  EXPECT_EQ(*cit2, 3);
  EXPECT_TRUE(cit < cit2);
}

TYPED_TEST(packed_int_vec_test, range_for_reads_and_writes_through_iterators) {
  using vec_type = typename TypeParam::auto_type;
  using value_type = typename vec_type::value_type;

  vec_type vec;
  for (int i = 0; i < 5; ++i) {
    vec.push_back(i);
  }

  value_type sum{};
  for (auto v : vec) {
    sum = sum + v;
  }
  EXPECT_EQ(sum, 10);

  for (auto x : vec) {
    x = x + 1;
  }

  EXPECT_THAT(vec, ElementsAre(1, 2, 3, 4, 5));
}

TYPED_TEST(packed_int_vec_test, iterator_proxy_assignment_and_iter_swap_work) {
  using vec_type = typename TypeParam::auto_type;

  vec_type vec;
  for (int i = 0; i < 8; ++i) {
    vec.push_back(i + 1);
  }

  auto it = vec.begin();
  *(it + 1) = *(it + 6);

  std::ranges::iter_swap(vec.begin() + 2, vec.begin() + 5);

  EXPECT_THAT(vec, ElementsAre(1, 7, 6, 4, 5, 3, 7, 8));
}

TYPED_TEST(packed_int_vec_test, std_sort_works) {
  using vec_type = typename TypeParam::auto_type;
  using value_type = typename vec_type::value_type;

  vec_type vec;

  for (value_type v : {5, 1, 4, 2, 3}) {
    vec.push_back(v);
  }

  std::sort(vec.begin(), vec.end());

  EXPECT_THAT(vec, ElementsAre(1, 2, 3, 4, 5));
}

TYPED_TEST(packed_int_vec_test, ranges_sort_works) {
  using vec_type = typename TypeParam::auto_type;
  using value_type = typename vec_type::value_type;

  vec_type vec;

  for (value_type v : {5, 1, 4, 2, 3}) {
    vec.push_back(v);
  }

  std::ranges::sort(vec);

  EXPECT_THAT(vec, ElementsAre(1, 2, 3, 4, 5));
}

TYPED_TEST(packed_int_vec_test, erase_remove_idiom) {
  using vec_type = typename TypeParam::auto_type;

  vec_type vec;

  for (auto v : {8, 1, 2, 3, 4, 5, 6, 7, 8, 10, 8, 9, 6}) {
    vec.push_back(v);
  }

  vec.erase(
      std::remove_if(vec.begin(), vec.end(), [](auto v) { return v % 2 != 0; }),
      vec.end());

  EXPECT_THAT(vec, ElementsAre(8, 2, 4, 6, 8, 10, 8, 6));

  vec.erase(std::remove(vec.begin(), vec.end(), 8), vec.end());

  EXPECT_THAT(vec, ElementsAre(2, 4, 6, 10, 6));
}

TYPED_TEST(packed_int_vec_test, erase_single_iterator) {
  using vec_type = typename TypeParam::auto_type;

  vec_type vec;

  for (int i = 0; i < 5; ++i) {
    vec.push_back(i + 1);
  }

  {
    auto it = vec.erase(vec.cbegin() + 2);
    EXPECT_EQ(*it, 4);
    EXPECT_THAT(vec, ElementsAre(1, 2, 4, 5));
    *it = 42; // we should have gotten a non-const iterator back
    EXPECT_THAT(vec, ElementsAre(1, 2, 42, 5));
  }

  {
    auto it = vec.erase(vec.begin());
    EXPECT_EQ(*it, 2);
    EXPECT_THAT(vec, ElementsAre(2, 42, 5));
  }

  {
    auto it = vec.erase(vec.end() - 1);
    EXPECT_EQ(it, vec.end());
    EXPECT_THAT(vec, ElementsAre(2, 42));
  }

  {
    auto it = vec.erase(vec.begin() + 1);
    EXPECT_EQ(it, vec.end());
    EXPECT_THAT(vec, ElementsAre(2));
  }

  {
    auto it = vec.erase(vec.begin());
    EXPECT_EQ(it, vec.end());
    EXPECT_TRUE(vec.empty());
  }
}

TYPED_TEST(packed_int_vec_test, erase_two_iterators) {
  using vec_type = typename TypeParam::auto_type;

  vec_type vec;

  for (int i = 0; i < 5; ++i) {
    vec.push_back(i + 1);
  }

  {
    auto it = vec.erase(vec.cbegin() + 1, vec.cbegin() + 4);
    EXPECT_EQ(*it, 5);
    EXPECT_THAT(vec, ElementsAre(1, 5));
    *it = 42; // we should have gotten a non-const iterator back
    EXPECT_THAT(vec, ElementsAre(1, 42));
  }

  {
    auto it = vec.erase(vec.begin(), vec.end());
    EXPECT_EQ(it, vec.end());
    EXPECT_TRUE(vec.empty());
  }
}

TYPED_TEST(packed_int_vec_test, reverse_iterators_work) {
  using vec_type = typename TypeParam::auto_type;
  using value_type = typename vec_type::value_type;

  vec_type vec;
  for (int i = 0; i < 5; ++i) {
    vec.push_back(i + 1);
  }

  std::vector<value_type> reversed;
  for (auto it = vec.rbegin(); it != vec.rend(); ++it) {
    reversed.push_back(*it);
  }

  EXPECT_THAT(reversed, ElementsAre(5, 4, 3, 2, 1));
}

TYPED_TEST(packed_int_vec_test, ranges_views_adaptors_work) {
  using vec_type = typename TypeParam::auto_type;
  using value_type = typename vec_type::value_type;

  vec_type vec;
  for (int i = 0; i < 6; ++i) {
    vec.push_back(i + 1);
  }

  auto view = vec | std::views::drop(1) | std::views::take(3);

  std::vector<value_type> values;
  for (auto v : view) {
    values.push_back(v);
  }

  EXPECT_THAT(values, ElementsAre(2, 3, 4));
}

TYPED_TEST(packed_int_vec_test, const_ranges_views_adaptors_work) {
  using vec_type = typename TypeParam::auto_type;
  using value_type = typename vec_type::value_type;

  vec_type vec;
  for (int i = 0; i < 6; ++i) {
    vec.push_back(i + 1);
  }

  vec_type const& cvec = vec;
  auto view = cvec | std::views::reverse | std::views::take(3);

  std::vector<value_type> values;
  for (auto v : view) {
    values.push_back(v);
  }

  EXPECT_THAT(values, ElementsAre(6, 5, 4));
}

TYPED_TEST(packed_int_vec_test,
           begin_end_cbegin_cend_match_expected_positions) {
  using vec_type = typename TypeParam::auto_type;

  vec_type vec;
  for (int i = 0; i < 4; ++i) {
    vec.push_back(i + 1);
  }

  EXPECT_EQ(std::distance(vec.begin(), vec.end()), 4);
  EXPECT_EQ(std::distance(vec.cbegin(), vec.cend()), 4);
  EXPECT_EQ(vec.begin() + vec.size(), vec.end());
  EXPECT_EQ(vec.cbegin() + vec.size(), vec.cend());
}

TYPED_TEST(packed_int_vec_test,
           iterator_remains_usable_across_repacking_storage_changes) {
  using vec_type = typename TypeParam::auto_type;
  using value_type = typename vec_type::value_type;

  vec_type vec;
  vec.push_back(1);
  vec.push_back(2);
  vec.push_back(3);

  auto it = vec.begin() + 1;

  // This may widen and repack.
  vec[2] = std::numeric_limits<value_type>::max();

  EXPECT_EQ(*it, 2);

  vec.reserve(vec.size() + 10);
  EXPECT_EQ(*it, 2);
}

TYPED_TEST(packed_int_vec_test,
           iterator_postfix_decrement_and_symmetric_arithmetic_work) {
  using vec_type = typename TypeParam::auto_type;

  vec_type vec;
  for (int i = 0; i < 6; ++i) {
    vec.push_back(i + 1);
  }

  auto it = 2 + vec.begin(); // friend operator+(difference_type, iterator)
  EXPECT_EQ(*it, 3);

  auto it2 = it - 1; // friend operator-(iterator, difference_type)
  EXPECT_EQ(*it2, 2);

  auto old = it--; // postfix --
  EXPECT_EQ(*old, 3);
  EXPECT_EQ(*it, 2);

  auto moved = std::ranges::iter_move(it);
  EXPECT_EQ(moved, 2);
}

TYPED_TEST(
    packed_int_vec_test,
    const_iterator_conversion_postfix_ops_and_symmetric_arithmetic_work) {
  using vec_type = typename TypeParam::auto_type;

  vec_type vec;
  for (int i = 0; i < 6; ++i) {
    vec.push_back(i + 1);
  }

  typename vec_type::const_iterator cit = vec.begin() + 1;
  EXPECT_EQ(*cit, 2);

  auto old_inc = cit++; // postfix ++
  EXPECT_EQ(*old_inc, 2);
  EXPECT_EQ(*cit, 3);

  auto old_dec = cit--; // postfix --
  EXPECT_EQ(*old_dec, 3);
  EXPECT_EQ(*cit, 2);

  auto cit2 = 3 + cit; // friend operator+(difference_type, const_iterator)
  EXPECT_EQ(*cit2, 5);

  auto cit3 = cit2 - 2; // friend operator-(const_iterator, difference_type)
  EXPECT_EQ(*cit3, 3);

  auto moved = std::ranges::iter_move(cit3);
  EXPECT_EQ(moved, 3);
}

TYPED_TEST(packed_int_vec_test,
           iterator_and_const_iterator_default_construct_and_compare) {
  using vec_type = typename TypeParam::auto_type;

  typename vec_type::iterator it1, it2;
  typename vec_type::const_iterator cit1, cit2;

  EXPECT_EQ(it1, it2);
  EXPECT_EQ(cit1, cit2);
}

TYPED_TEST(packed_int_vec_test, insert_count_value_into_empty_vector) {
  using vec_type = typename TypeParam::type;

  vec_type vec(0);

  auto it = vec.insert(vec.cbegin(), 3, 0);

  EXPECT_EQ(it - vec.begin(), 0);
  EXPECT_EQ(vec.size(), 3);
  EXPECT_THAT(vec, ElementsAre(0, 0, 0));
}

TYPED_TEST(packed_int_vec_test, insert_count_value_at_begin_shifts_elements) {
  using vec_type = typename TypeParam::type;

  vec_type vec(std::min<std::size_t>(vec_type::bits_per_block, 5));
  for (auto v : {1, 2, 3}) {
    vec.push_back(v);
  }

  auto it = vec.insert(vec.cbegin(), 2, 7);

  EXPECT_EQ(it - vec.begin(), 0);
  EXPECT_THAT(vec, ElementsAre(7, 7, 1, 2, 3));
}

TYPED_TEST(packed_int_vec_test, insert_single_value_in_middle) {
  using vec_type = typename TypeParam::type;

  vec_type vec(std::min<std::size_t>(vec_type::bits_per_block, 5));
  for (auto v : {1, 2, 3, 4}) {
    vec.push_back(v);
  }

  auto it = vec.insert(vec.cbegin() + 2, 9);

  EXPECT_EQ(it - vec.begin(), 2);
  EXPECT_THAT(vec, ElementsAre(1, 2, 9, 3, 4));
}

TYPED_TEST(packed_int_vec_test, insert_forward_range_in_middle) {
  using vec_type = typename TypeParam::type;
  using value_type = typename vec_type::value_type;

  vec_type vec(std::min<std::size_t>(vec_type::bits_per_block, 6));
  for (auto v : {1, 2, 5, 6}) {
    vec.push_back(v);
  }

  std::vector<value_type> src{
      3,
      4,
  };

  auto it = vec.insert(vec.cbegin() + 2, src.begin(), src.end());

  EXPECT_EQ(it - vec.begin(), 2);
  EXPECT_THAT(vec, ElementsAre(1, 2, 3, 4, 5, 6));
}

TYPED_TEST(packed_int_vec_test, insert_empty_forward_range_is_noop) {
  using vec_type = typename TypeParam::type;
  using value_type = typename vec_type::value_type;

  vec_type vec(std::min<std::size_t>(vec_type::bits_per_block, 5));
  for (auto v : {1, 2, 3}) {
    vec.push_back(v);
  }

  std::vector<value_type> empty;

  auto it = vec.insert(vec.cbegin() + 1, empty.begin(), empty.end());

  EXPECT_EQ(it - vec.begin(), 1);
  EXPECT_THAT(vec, ElementsAre(1, 2, 3));
}

TYPED_TEST(packed_int_vec_test, insert_single_pass_range_in_middle) {
  using vec_type = typename TypeParam::type;
  using value_type = typename vec_type::value_type;

  vec_type vec(std::min<std::size_t>(vec_type::bits_per_block, 6));
  for (auto v : {1, 4, 5}) {
    vec.push_back(v);
  }

  std::vector<value_type> src{
      2,
      3,
  };

  auto it = vec.insert(
      vec.cbegin() + 1, single_pass_input_iterator<value_type>{&src, 0},
      single_pass_input_iterator<value_type>{&src, src.size()});

  EXPECT_EQ(it - vec.begin(), 1);
  EXPECT_THAT(vec, ElementsAre(1, 2, 3, 4, 5));
}

TYPED_TEST(packed_int_vec_test, auto_insert_count_value_grows_bit_width) {
  using vec_type = typename TypeParam::auto_type;

  vec_type vec(0);
  vec.push_back(1);
  vec.push_back(2);

  auto const big = 31;
  auto const expected_bits = vec_type::required_bits(big);

  auto it = vec.insert(vec.cbegin() + 1, 2, big);

  EXPECT_EQ(it - vec.begin(), 1);
  EXPECT_EQ(vec.bits(), expected_bits);
  EXPECT_THAT(vec, ElementsAre(1, big, big, 2));
}

TYPED_TEST(packed_int_vec_test, auto_insert_forward_range_grows_bit_width) {
  using vec_type = typename TypeParam::auto_type;
  using value_type = typename vec_type::value_type;

  vec_type vec(0);
  vec.push_back(1);
  vec.push_back(2);

  std::vector<value_type> src{
      7,
      31,
  };

  auto it = vec.insert(vec.cbegin() + 1, src.begin(), src.end());

  EXPECT_EQ(it - vec.begin(), 1);
  EXPECT_EQ(vec.bits(),
            std::max(vec_type::required_bits(2), vec_type::required_bits(31)));
  EXPECT_THAT(vec, ElementsAre(1, 7, 31, 2));
}

TYPED_TEST(packed_int_vec_test, auto_insert_single_pass_range_grows_bit_width) {
  using vec_type = typename TypeParam::auto_type;
  using value_type = typename vec_type::value_type;

  vec_type vec(0);
  vec.push_back(1);
  vec.push_back(2);

  std::vector<value_type> src{
      7,
      31,
  };

  auto it = vec.insert(
      vec.cbegin() + 1, single_pass_input_iterator<value_type>{&src, 0},
      single_pass_input_iterator<value_type>{&src, src.size()});

  EXPECT_EQ(it - vec.begin(), 1);
  EXPECT_EQ(vec.bits(),
            std::max(vec_type::required_bits(2), vec_type::required_bits(31)));
  EXPECT_THAT(vec, ElementsAre(1, 7, 31, 2));
}

TYPED_TEST(packed_int_vec_test, insert_exceeding_max_size_throws) {
  using vec_type = typename TypeParam::type;

  vec_type vec(0);
  vec.resize(vec_type::max_size() - 1);

  EXPECT_THROW(vec.insert(vec.cbegin(), 2, 0), std::length_error);
}

TYPED_TEST(packed_int_vec_test,
           initializer_list_constructor_deduces_bits_and_values) {
  using vec_type = typename TypeParam::type;

  vec_type vec{1, 2, 3, 7};

  EXPECT_EQ(vec.size(), 4);
  EXPECT_EQ(vec.bits(), vec_type::required_bits(7));
  EXPECT_THAT(vec, ElementsAre(1, 2, 3, 7));
}

TYPED_TEST(packed_int_vec_test,
           auto_initializer_list_constructor_deduces_bits_and_values) {
  using vec_type = typename TypeParam::auto_type;
  using value_type = typename vec_type::value_type;
  static constexpr auto max = std::numeric_limits<value_type>::max();

  vec_type vec{1, max, 3, 31};

  EXPECT_EQ(vec.size(), 4);
  EXPECT_EQ(vec.bits(), vec_type::required_bits(max));
  EXPECT_THAT(vec, ElementsAre(1, max, 3, 31));
}

TYPED_TEST(packed_int_vec_test, insert_initializer_list_in_middle) {
  using vec_type = typename TypeParam::type;

  vec_type vec{1, 2, 5, 6};

  auto it = vec.insert(vec.cbegin() + 2, {3, 4});

  EXPECT_EQ(it - vec.begin(), 2);
  EXPECT_THAT(vec, ElementsAre(1, 2, 3, 4, 5, 6));
}

TYPED_TEST(packed_int_vec_test, insert_empty_initializer_list_is_noop) {
  using vec_type = typename TypeParam::type;

  vec_type vec{1, 2, 3};

  auto it = vec.insert(vec.cbegin() + 1, {});

  EXPECT_EQ(it - vec.begin(), 1);
  EXPECT_THAT(vec, ElementsAre(1, 2, 3));
}

TYPED_TEST(packed_int_vec_test, auto_insert_initializer_list_grows_bits) {
  using vec_type = typename TypeParam::auto_type;

  vec_type vec{1, 2};

  auto it = vec.insert(vec.cbegin() + 1, {7, 31});

  EXPECT_EQ(it - vec.begin(), 1);
  EXPECT_EQ(vec.bits(), vec_type::required_bits(31));
  EXPECT_THAT(vec, ElementsAre(1, 7, 31, 2));
}

TYPED_TEST(packed_int_vec_test, assign_count_value_replaces_contents) {
  using vec_type = typename TypeParam::type;

  vec_type vec{1, 2, 3, 4};
  vec.assign(3, 7);

  EXPECT_EQ(vec.size(), 3);
  EXPECT_THAT(vec, ElementsAre(7, 7, 7));
}

TYPED_TEST(packed_int_vec_test, assign_forward_range_replaces_contents) {
  using vec_type = typename TypeParam::type;
  using value_type = typename vec_type::value_type;

  vec_type vec{1, 2, 3, 4};
  std::vector<value_type> src{5, 6, 7};

  vec.assign(src.begin(), src.end());

  EXPECT_EQ(vec.size(), 3);
  EXPECT_THAT(vec, ElementsAre(5, 6, 7));
}

TYPED_TEST(packed_int_vec_test, assign_initializer_list_replaces_contents) {
  using vec_type = typename TypeParam::auto_type;

  vec_type vec{1, 2, 3, 4};
  vec.assign({8, 9});

  EXPECT_EQ(vec.size(), 2);
  EXPECT_THAT(vec, ElementsAre(8, 9));
}

TYPED_TEST(packed_int_vec_test,
           assign_initializer_list_replaces_and_truncates) {
  using vec_type = typename TypeParam::type;

  vec_type vec{1, 2, 3, 4};
  vec.assign({8, 9});

  vec_type expected(vec.bits());
  expected.push_back(8);
  expected.push_back(9);

  EXPECT_EQ(vec.size(), 2);
  EXPECT_THAT(vec, ElementsAreArray(expected));
}

TYPED_TEST(packed_int_vec_test, assign_single_pass_range_replaces_contents) {
  using vec_type = typename TypeParam::type;
  using value_type = typename vec_type::value_type;

  vec_type vec{1, 4, 5};

  std::vector<value_type> src{2, 3};
  vec.assign(single_pass_input_iterator<value_type>{&src, 0},
             single_pass_input_iterator<value_type>{&src, src.size()});

  EXPECT_EQ(vec.size(), 2);
  EXPECT_THAT(vec, ElementsAre(2, 3));
}

TYPED_TEST(packed_int_vec_test, auto_assign_forward_range_grows_bits) {
  using vec_type = typename TypeParam::auto_type;
  using value_type = typename vec_type::value_type;

  vec_type vec{1, 2};
  std::vector<value_type> src{7, 31};

  vec.assign(src.begin(), src.end());

  EXPECT_EQ(vec.size(), 2);
  EXPECT_EQ(vec.bits(), vec_type::required_bits(31));
  EXPECT_THAT(vec, ElementsAre(7, 31));
}

TYPED_TEST(packed_int_vec_test, auto_assign_single_pass_range_grows_bits) {
  using vec_type = typename TypeParam::auto_type;
  using value_type = typename vec_type::value_type;

  vec_type vec{1, 2};

  std::vector<value_type> src{7, 31};
  vec.assign(single_pass_input_iterator<value_type>{&src, 0},
             single_pass_input_iterator<value_type>{&src, src.size()});

  EXPECT_EQ(vec.size(), 2);
  EXPECT_EQ(vec.bits(), vec_type::required_bits(31));
  EXPECT_THAT(vec, ElementsAre(7, 31));
}

TYPED_TEST(packed_int_vec_test, assign_empty_initializer_list_clears_vector) {
  using vec_type = typename TypeParam::type;

  vec_type vec{1, 2, 3};
  vec.assign({});

  EXPECT_TRUE(vec.empty());
  EXPECT_EQ(vec.size(), 0);
}

TYPED_TEST(packed_int_vec_test, from_range_constructor_builds_vector) {
  using vec_type = typename TypeParam::type;
  using value_type = typename vec_type::value_type;

  std::array<value_type, 4> src{1, 2, 3, 31};

  vec_type vec(from_range, src);

  EXPECT_EQ(vec.size(), src.size());
  EXPECT_EQ(vec.bits(), vec_type::required_bits(31));
  EXPECT_THAT(vec, ElementsAre(1, 2, 3, 31));
}

TYPED_TEST(packed_int_vec_test, auto_from_range_constructor_builds_vector) {
  using vec_type = typename TypeParam::auto_type;
  using value_type = typename vec_type::value_type;

  std::array<value_type, 4> src{1, 2, 3, 31};

  vec_type vec(from_range, src);

  EXPECT_EQ(vec.size(), src.size());
  EXPECT_EQ(vec.bits(), vec_type::required_bits(31));
  EXPECT_THAT(vec, ElementsAre(1, 2, 3, 31));
}

TYPED_TEST(packed_int_vec_test, insert_range_with_forward_range) {
  using vec_type = typename TypeParam::type;
  using value_type = typename vec_type::value_type;

  vec_type vec{1, 2, 5, 6};
  std::array<value_type, 2> src{3, 4};

  auto it = vec.insert_range(vec.cbegin() + 2, src);

  EXPECT_EQ(it - vec.begin(), 2);
  EXPECT_THAT(vec, ElementsAre(1, 2, 3, 4, 5, 6));
}

TYPED_TEST(packed_int_vec_test, append_range_with_forward_range) {
  using vec_type = typename TypeParam::auto_type;
  using value_type = typename vec_type::value_type;

  vec_type vec{1, 2};
  std::array<value_type, 3> src{3, 4, 5};

  vec.append_range(src);

  EXPECT_THAT(vec, ElementsAre(1, 2, 3, 4, 5));
}

TYPED_TEST(packed_int_vec_test, assign_range_with_forward_range) {
  using vec_type = typename TypeParam::auto_type;
  using value_type = typename vec_type::value_type;

  vec_type vec{1, 2, 3, 4};
  std::array<value_type, 2> src{7, 8};

  vec.assign_range(src);

  EXPECT_EQ(vec.size(), 2);
  EXPECT_THAT(vec, ElementsAre(7, 8));
}

TYPED_TEST(packed_int_vec_test, assign_range_with_forward_range_truncates) {
  using vec_type = typename TypeParam::type;
  using value_type = typename vec_type::value_type;

  vec_type vec{1, 2, 3, 4};
  std::array<value_type, 2> src{7, 8};

  vec.assign_range(src);

  vec_type expected(vec.bits());
  expected.push_back(7);
  expected.push_back(8);

  EXPECT_EQ(vec.size(), 2);
  EXPECT_THAT(vec, ElementsAreArray(expected));
}

TYPED_TEST(packed_int_vec_test, insert_range_with_single_pass_input_range) {
  using vec_type = typename TypeParam::type;
  using value_type = typename vec_type::value_type;

  vec_type vec{1, 4, 5};
  single_pass_input_range<value_type> src{{2, 3}};

  auto it = vec.insert_range(vec.cbegin() + 1, src);

  EXPECT_EQ(it - vec.begin(), 1);
  EXPECT_THAT(vec, ElementsAre(1, 2, 3, 4, 5));
}

TYPED_TEST(packed_int_vec_test, append_range_with_single_pass_input_range) {
  using vec_type = typename TypeParam::auto_type;
  using value_type = typename vec_type::value_type;

  vec_type vec{1, 2};
  single_pass_input_range<value_type> src{{3, 4, 5}};

  vec.append_range(src);

  EXPECT_THAT(vec, ElementsAre(1, 2, 3, 4, 5));
}

TYPED_TEST(packed_int_vec_test, assign_range_with_single_pass_input_range) {
  using vec_type = typename TypeParam::auto_type;
  using value_type = typename vec_type::value_type;

  vec_type vec{1, 2, 3};
  single_pass_input_range<value_type> src{{7, 8}};

  vec.assign_range(src);

  EXPECT_EQ(vec.size(), 2);
  EXPECT_THAT(vec, ElementsAre(7, 8));
}

TYPED_TEST(packed_int_vec_test, auto_insert_range_grows_bits) {
  using vec_type = typename TypeParam::auto_type;
  using value_type = typename vec_type::value_type;

  vec_type vec{1, 2};
  std::array<value_type, 2> src{7, 31};

  auto it = vec.insert_range(vec.cbegin() + 1, src);

  EXPECT_EQ(it - vec.begin(), 1);
  EXPECT_EQ(vec.bits(), vec_type::required_bits(31));
  EXPECT_THAT(vec, ElementsAre(1, 7, 31, 2));
}

TYPED_TEST(packed_int_vec_test, auto_append_range_grows_bits) {
  using vec_type = typename TypeParam::auto_type;
  using value_type = typename vec_type::value_type;

  vec_type vec{1, 2};
  std::array<value_type, 2> src{7, 31};

  vec.append_range(src);

  EXPECT_EQ(vec.bits(), vec_type::required_bits(31));
  EXPECT_THAT(vec, ElementsAre(1, 2, 7, 31));
}

TYPED_TEST(packed_int_vec_test, auto_assign_range_grows_bits) {
  using vec_type = typename TypeParam::auto_type;
  using value_type = typename vec_type::value_type;

  vec_type vec{1, 2};
  std::array<value_type, 2> src{7, 31};

  vec.assign_range(src);

  EXPECT_EQ(vec.size(), 2);
  EXPECT_EQ(vec.bits(), vec_type::required_bits(31));
  EXPECT_THAT(vec, ElementsAre(7, 31));
}

TYPED_TEST(packed_int_vec_test, insert_range_empty_forward_range_is_noop) {
  using vec_type = typename TypeParam::type;
  using value_type = typename vec_type::value_type;

  vec_type vec{1, 2, 3};
  std::array<value_type, 0> src{};

  auto it = vec.insert_range(vec.cbegin() + 1, src);

  EXPECT_EQ(it - vec.begin(), 1);
  EXPECT_THAT(vec, ElementsAre(1, 2, 3));
}
