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

#include <tuple>

#include <dwarfs/container/packed_value_traits_optional.h>

#include "packed_int_vector_test_helpers.h"

using namespace dwarfs::test;
using namespace dwarfs::container;
using std::get;
using ::testing::ElementsAre;
using ::testing::ElementsAreArray;

namespace {

template <typename Vec>
auto max_widths_of(std::initializer_list<typename Vec::value_type> values) ->
    typename Vec::widths_type {
  using widths_type = typename Vec::widths_type;

  widths_type result{};
  result.fill(0);

  for (auto const& value : values) {
    auto const w = Vec::required_widths(value);
    for (std::size_t i = 0; i < result.size(); ++i) {
      result[i] = std::max(result[i], w[i]);
    }
  }

  return result;
}

struct packed_int_vector_selector {
  template <dwarfs::container::packed_vector_value T>
  using type = packed_int_vector<T>;

  template <dwarfs::container::packed_vector_value T>
  using auto_type = auto_packed_int_vector<T>;
};

struct compact_packed_int_vector_selector {
  template <dwarfs::container::packed_vector_value T>
  using type = compact_packed_int_vector<T>;

  template <dwarfs::container::packed_vector_value T>
  using auto_type = compact_auto_packed_int_vector<T>;
};

using tuple_type = std::tuple<uint16_t, int16_t, uint32_t>;

enum class small_enum : uint8_t {
  zero = 0,
  one = 1,
  big = 17,
};

using trait_tuple_type =
    std::tuple<small_enum, std::optional<uint16_t>, uint32_t>;

} // namespace

template <typename VecSelector>
class packed_int_vec_tuple_test : public ::testing::Test {};

using packed_tuple_vector_types =
    ::testing::Types<packed_int_vector_selector,
                     compact_packed_int_vector_selector>;

TYPED_TEST_SUITE(packed_int_vec_tuple_test, packed_tuple_vector_types);

TYPED_TEST(packed_int_vec_tuple_test, basic_fixed_width) {
  using vec_type = typename TypeParam::template type<tuple_type>;
  using widths_type = typename vec_type::widths_type;

  vec_type vec(widths_type{6, 9, 12});
  vec.push_back({3, -7, 42});
  vec.push_back({17, 12, 511});

  EXPECT_EQ(vec.size(), 2);
  EXPECT_EQ(vec.widths(), (widths_type{6, 9, 12}));

  EXPECT_EQ(vec[0], (tuple_type{3, -7, 42}));
  EXPECT_EQ(vec[1], (tuple_type{17, 12, 511}));
}

TYPED_TEST(packed_int_vec_tuple_test, automatic_width_growth) {
  using vec_type = typename TypeParam::template auto_type<tuple_type>;
  using widths_type = typename vec_type::widths_type;

  vec_type vec;
  EXPECT_EQ(vec.widths(), (widths_type{0, 0, 0}));

  vec.push_back({1, 0, 3});
  EXPECT_EQ(vec.widths(), (widths_type{
                              vec_type::required_widths(tuple_type{1, 0, 3})[0],
                              vec_type::required_widths(tuple_type{1, 0, 3})[1],
                              vec_type::required_widths(tuple_type{1, 0, 3})[2],
                          }));

  vec.push_back({255, -200, 70000});
  auto const expected = vec_type::required_widths(tuple_type{255, -200, 70000});
  EXPECT_EQ(vec.widths(), expected);

  EXPECT_EQ(vec[0], (tuple_type{1, 0, 3}));
  EXPECT_EQ(vec[1], (tuple_type{255, -200, 70000}));
}

TYPED_TEST(packed_int_vec_tuple_test, proxy_assignment_replaces_whole_tuple) {
  using vec_type = typename TypeParam::template auto_type<tuple_type>;
  using widths_type = typename vec_type::widths_type;

  vec_type vec;
  vec.push_back({1, 2, 3});
  vec.push_back({4, 5, 6});

  vec[0] = tuple_type{100, -50, 1000};

  EXPECT_EQ(vec[0], (tuple_type{100, -50, 1000}));
  EXPECT_EQ(vec[1], (tuple_type{4, 5, 6}));
  EXPECT_EQ(vec.widths(), ([] {
              widths_type w{};
              auto a = vec_type::required_widths(tuple_type{100, -50, 1000});
              auto b = vec_type::required_widths(tuple_type{4, 5, 6});
              for (std::size_t i = 0; i < w.size(); ++i) {
                w[i] = std::max(a[i], b[i]);
              }
              return w;
            })());
}

TYPED_TEST(packed_int_vec_tuple_test, resize_with_tuple_value) {
  using vec_type = typename TypeParam::template auto_type<tuple_type>;

  vec_type vec;
  vec.push_back({1, -2, 3});
  vec.resize(4, tuple_type{7, -9, 123});

  EXPECT_EQ(vec.size(), 4);
  EXPECT_EQ(vec.get(0), (tuple_type{1, -2, 3}));
  EXPECT_EQ(vec.get(1), (tuple_type{7, -9, 123}));
  EXPECT_EQ(vec.get(2), (tuple_type{7, -9, 123}));
  EXPECT_EQ(vec.get(3), (tuple_type{7, -9, 123}));

  EXPECT_EQ(vec.widths(), (max_widths_of<vec_type>({
                              tuple_type{1, -2, 3},
                              tuple_type{7, -9, 123},
                          })));
}

TYPED_TEST(packed_int_vec_tuple_test, insert_and_erase) {
  using vec_type = typename TypeParam::template type<tuple_type>;
  using widths_type = typename vec_type::widths_type;

  vec_type vec(widths_type{10, 10, 16});
  vec.push_back({1, 2, 3});
  vec.push_back({7, -8, 9});
  vec.push_back({10, 11, 12});

  vec.insert(vec.begin() + 1, tuple_type{100, -50, 999});
  EXPECT_THAT(vec.unpack(),
              ElementsAre(tuple_type{1, 2, 3}, tuple_type{100, -50, 999},
                          tuple_type{7, -8, 9}, tuple_type{10, 11, 12}));

  vec.erase(vec.begin() + 2);
  EXPECT_THAT(vec.unpack(),
              ElementsAre(tuple_type{1, 2, 3}, tuple_type{100, -50, 999},
                          tuple_type{10, 11, 12}));

  vec.erase(vec.begin(), vec.begin() + 2);
  EXPECT_THAT(vec.unpack(), ElementsAre(tuple_type{10, 11, 12}));
}

TYPED_TEST(packed_int_vec_tuple_test, copy_and_move) {
  using vec_type = typename TypeParam::template auto_type<tuple_type>;

  vec_type vec;
  vec.push_back({1, -2, 3});
  vec.push_back({100, -200, 30000});

  auto const expected_widths = vec.widths();

  vec_type copy(vec);
  EXPECT_EQ(copy.size(), 2);
  EXPECT_EQ(copy.widths(), expected_widths);
  EXPECT_THAT(copy.unpack(),
              ElementsAre(tuple_type{1, -2, 3}, tuple_type{100, -200, 30000}));

  vec_type moved(std::move(copy));
  EXPECT_EQ(moved.size(), 2);
  EXPECT_EQ(moved.widths(), expected_widths);
  EXPECT_THAT(moved.unpack(),
              ElementsAre(tuple_type{1, -2, 3}, tuple_type{100, -200, 30000}));

  EXPECT_TRUE(copy.empty());
}

TYPED_TEST(packed_int_vec_tuple_test, unpack_roundtrip) {
  using vec_type = typename TypeParam::template type<tuple_type>;
  using widths_type = typename vec_type::widths_type;

  vec_type vec(widths_type{10, 10, 20});
  vec.push_back({1, 2, 3});
  vec.push_back({4, -5, 6});
  vec.push_back({7, 8, 900});

  auto unpacked = vec.unpack();
  EXPECT_THAT(unpacked, ElementsAre(tuple_type{1, 2, 3}, tuple_type{4, -5, 6},
                                    tuple_type{7, 8, 900}));
}

TYPED_TEST(packed_int_vec_tuple_test, required_widths_of_vector) {
  using vec_type = typename TypeParam::template auto_type<tuple_type>;

  vec_type vec;
  vec.push_back({1, -2, 3});
  vec.push_back({255, -200, 70000});
  vec.push_back({17, 0, 12});

  EXPECT_EQ(vec.required_widths(), (max_widths_of<vec_type>({
                                       tuple_type{1, -2, 3},
                                       tuple_type{255, -200, 70000},
                                       tuple_type{17, 0, 12},
                                   })));
}

TYPED_TEST(packed_int_vec_tuple_test, truncate_to_widths) {
  using vec_type = typename TypeParam::template type<tuple_type>;
  using widths_type = typename vec_type::widths_type;

  vec_type vec(widths_type{16, 16, 32});
  vec.push_back({3, -7, 42});
  vec.push_back({12, 5, 511});
  vec.push_back({31, -8, 700});

  auto const expected_widths = max_widths_of<vec_type>({
      tuple_type{3, -7, 42},
      tuple_type{12, 5, 511},
      tuple_type{31, -8, 700},
  });

  vec.truncate_to_widths(expected_widths);

  EXPECT_EQ(vec.widths(), expected_widths);
  EXPECT_THAT(vec.unpack(),
              ElementsAre(tuple_type{3, -7, 42}, tuple_type{12, 5, 511},
                          tuple_type{31, -8, 700}));
}

TYPED_TEST(packed_int_vec_tuple_test, mixed_field_width_tuple_roundtrip) {
  using mixed_tuple_type = std::tuple<uint8_t, uint16_t, uint32_t>;
  using vec_type = typename TypeParam::template auto_type<mixed_tuple_type>;

  vec_type vec;
  vec.push_back({7, 300, 70000});
  vec.push_back({255, 1, 2});

  EXPECT_EQ(vec.get(0), (mixed_tuple_type{7, 300, 70000}));
  EXPECT_EQ(vec.get(1), (mixed_tuple_type{255, 1, 2}));

  EXPECT_EQ(vec.widths(), (max_widths_of<vec_type>({
                              mixed_tuple_type{7, 300, 70000},
                              mixed_tuple_type{255, 1, 2},
                          })));
}

TYPED_TEST(packed_int_vec_tuple_test, assign_initializer_list) {
  using vec_type = typename TypeParam::template auto_type<tuple_type>;

  vec_type vec;
  vec.assign({
      tuple_type{1, 2, 3},
      tuple_type{10, -20, 30},
      tuple_type{255, -200, 70000},
  });

  EXPECT_THAT(vec.unpack(),
              ElementsAre(tuple_type{1, 2, 3}, tuple_type{10, -20, 30},
                          tuple_type{255, -200, 70000}));

  EXPECT_EQ(vec.widths(), (max_widths_of<vec_type>({
                              tuple_type{1, 2, 3},
                              tuple_type{10, -20, 30},
                              tuple_type{255, -200, 70000},
                          })));
}

TYPED_TEST(packed_int_vec_tuple_test, reserve_and_shrink_to_fit) {
  using vec_type = typename TypeParam::template type<tuple_type>;
  using widths_type = typename vec_type::widths_type;

  vec_type vec(widths_type{10, 10, 20});
  vec.push_back({1, 2, 3});
  vec.push_back({10, -20, 30});
  vec.push_back({255, -200, 70000});

  auto const original = vec.unpack();

  vec.reserve(100);
  EXPECT_GE(vec.capacity(), 100);
  EXPECT_THAT(vec.unpack(), ElementsAreArray(original));

  vec.shrink_to_fit();
  EXPECT_GE(vec.capacity(), vec.size());
  EXPECT_THAT(vec.unpack(), ElementsAreArray(original));
}

TYPED_TEST(packed_int_vec_tuple_test, optimize_storage_reduces_widths) {
  using vec_type = typename TypeParam::template auto_type<tuple_type>;

  vec_type vec;
  vec.push_back({1, 2, 3});
  vec.push_back({255, -200, 70000});
  vec.push_back({4, 5, 6});

  auto const widened = vec.widths();

  vec.erase(vec.begin() + 1);
  auto const expected = max_widths_of<vec_type>({
      tuple_type{1, 2, 3},
      tuple_type{4, 5, 6},
  });

  EXPECT_NE(widened, expected);

  vec.optimize_storage();

  EXPECT_EQ(vec.widths(), expected);
  EXPECT_THAT(vec.unpack(),
              ElementsAre(tuple_type{1, 2, 3}, tuple_type{4, 5, 6}));
}

TYPED_TEST(packed_int_vec_tuple_test,
           optimize_storage_noop_when_already_optimal) {
  using vec_type = typename TypeParam::template auto_type<tuple_type>;

  vec_type vec;
  vec.push_back({1, -2, 3});
  vec.push_back({7, -9, 123});

  auto const expected = max_widths_of<vec_type>({
      tuple_type{1, -2, 3},
      tuple_type{7, -9, 123},
  });

  EXPECT_EQ(vec.widths(), expected);

  vec.optimize_storage();

  EXPECT_EQ(vec.widths(), expected);
  EXPECT_THAT(vec.unpack(),
              ElementsAre(tuple_type{1, -2, 3}, tuple_type{7, -9, 123}));
}

TYPED_TEST(packed_int_vec_tuple_test, auto_mixed_operations_stress) {
  using vec_type = typename TypeParam::template auto_type<tuple_type>;

  vec_type vec;

  vec.push_back({1, 2, 3});
  vec.push_back({10, -20, 30});
  vec.push_back({255, -200, 70000});

  vec.insert(vec.begin() + 1, tuple_type{7, 8, 9});
  vec[0] = tuple_type{100, -50, 1000};
  vec.erase(vec.begin() + 2);
  vec.resize(6, tuple_type{4, 5, 6});
  vec.pop_back();
  vec.push_back({17, -1, 999});

  auto const expected_values = std::vector<tuple_type>{
      tuple_type{100, -50, 1000},   tuple_type{7, 8, 9},
      tuple_type{255, -200, 70000}, tuple_type{4, 5, 6},
      tuple_type{4, 5, 6},          tuple_type{17, -1, 999},
  };

  EXPECT_THAT(vec.unpack(), ElementsAreArray(expected_values));

  typename vec_type::widths_type expected_widths{};
  expected_widths.fill(0);
  for (auto const& value : expected_values) {
    auto const w = vec_type::required_widths(value);
    for (std::size_t i = 0; i < expected_widths.size(); ++i) {
      expected_widths[i] = std::max(expected_widths[i], w[i]);
    }
  }

  EXPECT_EQ(vec.widths(), expected_widths);
}

TYPED_TEST(packed_int_vec_tuple_test, fixed_mixed_operations_stress) {
  using vec_type = typename TypeParam::template type<tuple_type>;
  using widths_type = typename vec_type::widths_type;

  vec_type vec(widths_type{10, 10, 20});

  vec.push_back({1, 2, 3});
  vec.push_back({7, -8, 9});
  vec.push_back({10, 11, 12});

  vec.insert(vec.begin() + 1, tuple_type{100, -50, 999});
  vec[3] = tuple_type{17, 18, 19};
  vec.erase(vec.begin());
  vec.resize(5, tuple_type{4, 5, 6});

  EXPECT_EQ(vec.widths(), (widths_type{10, 10, 20}));
  EXPECT_THAT(vec.unpack(),
              ElementsAre(tuple_type{100, -50, 999}, tuple_type{7, -8, 9},
                          tuple_type{17, 18, 19}, tuple_type{4, 5, 6},
                          tuple_type{4, 5, 6}));
}

TYPED_TEST(packed_int_vec_tuple_test, reserve_then_append_many) {
  using vec_type = typename TypeParam::template auto_type<tuple_type>;

  vec_type vec;
  vec.reserve(32);

  for (uint16_t i = 0; i < 20; ++i) {
    vec.push_back(tuple_type{i, static_cast<int16_t>(-static_cast<int>(i)),
                             static_cast<uint32_t>(i) * 1000});
  }

  EXPECT_EQ(vec.size(), 20);
  EXPECT_GE(vec.capacity(), 20);

  for (uint16_t i = 0; i < 20; ++i) {
    EXPECT_EQ(vec.get(i),
              (tuple_type{i, static_cast<int16_t>(-static_cast<int>(i)),
                          static_cast<uint32_t>(i) * 1000}));
  }
}

TYPED_TEST(packed_int_vec_tuple_test, set_then_optimize_storage) {
  using vec_type = typename TypeParam::template auto_type<tuple_type>;

  vec_type vec;
  vec.push_back({1, 2, 3});
  vec.push_back({4, 5, 6});

  vec[1] = tuple_type{255, -200, 70000};
  auto const widened = vec.widths();

  vec.erase(vec.begin() + 1);
  vec.optimize_storage();

  auto const expected = max_widths_of<vec_type>({
      tuple_type{1, 2, 3},
  });

  EXPECT_NE(widened, expected);
  EXPECT_EQ(vec.widths(), expected);
  EXPECT_THAT(vec.unpack(), ElementsAre(tuple_type{1, 2, 3}));
}

TYPED_TEST(packed_int_vec_tuple_test, reverse_iterators) {
  using vec_type = typename TypeParam::template auto_type<tuple_type>;

  vec_type vec;
  vec.push_back({1, 2, 3});
  vec.push_back({4, 5, 6});
  vec.push_back({7, 8, 9});

  std::vector<tuple_type> reversed;
  for (auto it = vec.rbegin(); it != vec.rend(); ++it) {
    reversed.push_back(*it);
  }
  EXPECT_THAT(reversed, ElementsAre(tuple_type{7, 8, 9}, tuple_type{4, 5, 6},
                                    tuple_type{1, 2, 3}));

  vec_type const& cvec = vec;
  reversed.clear();
  for (auto it = cvec.crbegin(); it != cvec.crend(); ++it) {
    reversed.push_back(*it);
  }
  EXPECT_THAT(reversed, ElementsAre(tuple_type{7, 8, 9}, tuple_type{4, 5, 6},
                                    tuple_type{1, 2, 3}));
}

TYPED_TEST(packed_int_vec_tuple_test, get_field_reads_individual_fields) {
  using vec_type = typename TypeParam::template type<tuple_type>;
  using widths_type = typename vec_type::widths_type;

  vec_type vec(widths_type{10, 10, 20});
  vec.push_back({7, -8, 999});

  EXPECT_EQ(vec.template get_field<0>(0), 7);
  EXPECT_EQ(vec.template get_field<1>(0), -8);
  EXPECT_EQ(vec.template get_field<2>(0), 999);
}

TYPED_TEST(packed_int_vec_tuple_test,
           set_field_updates_only_selected_field_fixed) {
  using vec_type = typename TypeParam::template type<tuple_type>;
  using widths_type = typename vec_type::widths_type;

  vec_type vec(widths_type{10, 10, 20});
  vec.push_back({1, 2, 3});
  vec.push_back({4, 5, 6});

  vec.template set_field<1>(0, -77);

  EXPECT_EQ(vec.get(0), (tuple_type{1, -77, 3}));
  EXPECT_EQ(vec.get(1), (tuple_type{4, 5, 6}));
  EXPECT_EQ(vec.widths(), (widths_type{10, 10, 20}));
}

TYPED_TEST(packed_int_vec_tuple_test, set_field_grows_only_touched_width_auto) {
  using vec_type = typename TypeParam::template auto_type<tuple_type>;

  vec_type vec;
  vec.push_back({1, 2, 3});
  vec.push_back({4, 5, 6});

  auto const before = vec.widths();

  vec.template set_field<1>(0, -200);

  auto const after = vec.widths();

  EXPECT_EQ(vec.get(0), (tuple_type{1, -200, 3}));
  EXPECT_EQ(vec.get(1), (tuple_type{4, 5, 6}));

  EXPECT_EQ(after[0], before[0]);
  EXPECT_GE(after[1], before[1]);
  EXPECT_EQ(after[2], before[2]);

  EXPECT_EQ(after, (max_widths_of<vec_type>({
                       tuple_type{1, -200, 3},
                       tuple_type{4, 5, 6},
                   })));
}

TYPED_TEST(packed_int_vec_tuple_test, set_field_can_grow_unsigned_field_auto) {
  using vec_type = typename TypeParam::template auto_type<tuple_type>;

  vec_type vec;
  vec.push_back({1, 2, 3});
  vec.push_back({4, 5, 6});

  vec.template set_field<2>(1, 70000);

  EXPECT_EQ(vec.get(0), (tuple_type{1, 2, 3}));
  EXPECT_EQ(vec.get(1), (tuple_type{4, 5, 70000}));

  EXPECT_EQ(vec.widths(), (max_widths_of<vec_type>({
                              tuple_type{1, 2, 3},
                              tuple_type{4, 5, 70000},
                          })));
}

TYPED_TEST(packed_int_vec_tuple_test, get_field_after_set_field_roundtrip) {
  using vec_type = typename TypeParam::template auto_type<tuple_type>;

  vec_type vec;
  vec.push_back({10, -20, 30});

  vec.template set_field<0>(0, 255);
  vec.template set_field<1>(0, -128);
  vec.template set_field<2>(0, 123456);

  EXPECT_EQ(vec.template get_field<0>(0), 255);
  EXPECT_EQ(vec.template get_field<1>(0), -128);
  EXPECT_EQ(vec.template get_field<2>(0), 123456);
  EXPECT_EQ(vec.get(0), (tuple_type{255, -128, 123456}));
}

TYPED_TEST(packed_int_vec_tuple_test,
           set_field_does_not_affect_other_elements) {
  using vec_type = typename TypeParam::template auto_type<tuple_type>;

  vec_type vec;
  vec.push_back({1, 2, 3});
  vec.push_back({10, 20, 30});
  vec.push_back({100, 200, 300});

  vec.template set_field<0>(1, 77);
  vec.template set_field<2>(1, 9999);

  EXPECT_THAT(vec.unpack(),
              ElementsAre(tuple_type{1, 2, 3}, tuple_type{77, 20, 9999},
                          tuple_type{100, 200, 300}));
}

TYPED_TEST(packed_int_vec_tuple_test,
           tuple_with_existing_packed_value_traits_roundtrip) {
  using trait_tuple_type =
      std::tuple<small_enum, std::optional<uint16_t>, uint32_t>;
  using vec_type = typename TypeParam::template auto_type<trait_tuple_type>;

  vec_type vec;
  vec.push_back({small_enum::one, std::nullopt, 3});
  vec.push_back({small_enum::big, std::optional<uint16_t>{42}, 70000});

  EXPECT_EQ(vec.get(0), (trait_tuple_type{small_enum::one, std::nullopt, 3}));
  EXPECT_EQ(vec.get(1), (trait_tuple_type{small_enum::big,
                                          std::optional<uint16_t>{42}, 70000}));
}

TYPED_TEST(packed_int_vec_tuple_test,
           tuple_with_existing_packed_value_traits_widths) {
  using trait_tuple_type =
      std::tuple<small_enum, std::optional<uint16_t>, uint32_t>;
  using vec_type = typename TypeParam::template auto_type<trait_tuple_type>;

  vec_type vec;
  vec.push_back({small_enum::one, std::nullopt, 3});
  vec.push_back({small_enum::big, std::optional<uint16_t>{42}, 70000});

  EXPECT_EQ(
      vec.widths(),
      (max_widths_of<vec_type>({
          trait_tuple_type{small_enum::one, std::nullopt, 3},
          trait_tuple_type{small_enum::big, std::optional<uint16_t>{42}, 70000},
      })));
}

TYPED_TEST(packed_int_vec_tuple_test,
           tuple_with_existing_packed_value_traits_field_access) {
  using trait_tuple_type =
      std::tuple<small_enum, std::optional<uint16_t>, uint32_t>;
  using vec_type = typename TypeParam::template auto_type<trait_tuple_type>;

  vec_type vec;
  vec.push_back({small_enum::one, std::nullopt, 3});

  EXPECT_EQ(vec.template get_field<0>(0), small_enum::one);
  EXPECT_EQ(vec.template get_field<1>(0), std::nullopt);
  EXPECT_EQ(vec.template get_field<2>(0), 3);

  vec.template set_field<0>(0, small_enum::big);
  vec.template set_field<1>(0, std::optional<uint16_t>{99});
  vec.template set_field<2>(0, 123456);

  EXPECT_EQ(vec.get(0), (trait_tuple_type{
                            small_enum::big,
                            std::optional<uint16_t>{99},
                            123456,
                        }));
}

TYPED_TEST(packed_int_vec_tuple_test, proxy_get_reads_individual_fields) {
  using vec_type = typename TypeParam::template type<tuple_type>;
  using widths_type = typename vec_type::widths_type;

  vec_type vec(widths_type{10, 10, 20});
  vec.push_back({7, -8, 999});

  EXPECT_EQ(get<0>(vec[0]), 7);
  EXPECT_EQ(get<1>(vec[0]), -8);
  EXPECT_EQ(get<2>(vec[0]), 999);
}

TYPED_TEST(packed_int_vec_tuple_test,
           proxy_get_writes_individual_fields_fixed) {
  using vec_type = typename TypeParam::template type<tuple_type>;
  using widths_type = typename vec_type::widths_type;

  vec_type vec(widths_type{10, 10, 20});
  vec.push_back({1, 2, 3});
  vec.push_back({4, 5, 6});

  get<1>(vec[0]) = -77;
  get<2>(vec[1]) = 999;

  EXPECT_EQ(vec.get(0), (tuple_type{1, -77, 3}));
  EXPECT_EQ(vec.get(1), (tuple_type{4, 5, 999}));
  EXPECT_EQ(vec.widths(), (widths_type{10, 10, 20}));
}

TYPED_TEST(packed_int_vec_tuple_test,
           proxy_get_writes_grow_only_touched_field_auto) {
  using vec_type = typename TypeParam::template auto_type<tuple_type>;

  vec_type vec;
  vec.push_back({1, 2, 3});
  vec.push_back({4, 5, 6});

  auto const before = vec.widths();

  get<1>(vec[0]) = -200;

  auto const after = vec.widths();

  EXPECT_EQ(vec.get(0), (tuple_type{1, -200, 3}));
  EXPECT_EQ(vec.get(1), (tuple_type{4, 5, 6}));

  EXPECT_EQ(after[0], before[0]);
  EXPECT_GE(after[1], before[1]);
  EXPECT_EQ(after[2], before[2]);

  EXPECT_EQ(after, (max_widths_of<vec_type>({
                       tuple_type{1, -200, 3},
                       tuple_type{4, 5, 6},
                   })));
}

TYPED_TEST(packed_int_vec_tuple_test, proxy_get_writes_unsigned_field_auto) {
  using vec_type = typename TypeParam::template auto_type<tuple_type>;

  vec_type vec;
  vec.push_back({1, 2, 3});
  vec.push_back({4, 5, 6});

  get<2>(vec[1]) = 70000;

  EXPECT_EQ(vec.get(0), (tuple_type{1, 2, 3}));
  EXPECT_EQ(vec.get(1), (tuple_type{4, 5, 70000}));

  EXPECT_EQ(vec.widths(), (max_widths_of<vec_type>({
                              tuple_type{1, 2, 3},
                              tuple_type{4, 5, 70000},
                          })));
}

TYPED_TEST(packed_int_vec_tuple_test,
           proxy_get_does_not_affect_other_fields_or_elements) {
  using vec_type = typename TypeParam::template auto_type<tuple_type>;

  vec_type vec;
  vec.push_back({10, 20, 30});
  vec.push_back({40, 50, 60});
  vec.push_back({70, 80, 90});

  get<0>(vec[1]) = 111;
  get<2>(vec[1]) = 9999;

  EXPECT_THAT(vec.unpack(),
              ElementsAre(tuple_type{10, 20, 30}, tuple_type{111, 50, 9999},
                          tuple_type{70, 80, 90}));
}

TYPED_TEST(packed_int_vec_tuple_test, proxy_get_const_vector_reads_by_value) {
  using vec_type = typename TypeParam::template auto_type<tuple_type>;

  vec_type vec;
  vec.push_back({7, -8, 999});

  vec_type const& cvec = vec;

  EXPECT_EQ(get<0>(cvec[0]), 7);
  EXPECT_EQ(get<1>(cvec[0]), -8);
  EXPECT_EQ(get<2>(cvec[0]), 999);
}

TYPED_TEST(packed_int_vec_tuple_test, value_proxy_const_assignment) {
  using vec_type = typename TypeParam::template auto_type<tuple_type>;

  vec_type vec;
  vec.push_back({1, 2, 3});
  vec.push_back({4, 5, 6});

  auto const proxy = vec[0];
  proxy = tuple_type{100, -50, 1000};

  EXPECT_EQ(vec.get(0), (tuple_type{100, -50, 1000}));
  EXPECT_EQ(vec.get(1), (tuple_type{4, 5, 6}));
}

TYPED_TEST(packed_int_vec_tuple_test, value_proxy_comparisons) {
  using vec_type = typename TypeParam::template auto_type<tuple_type>;

  vec_type vec;
  vec.push_back({1, 2, 3});
  vec.push_back({1, 2, 4});

  tuple_type const v0{1, 2, 3};
  tuple_type const v1{1, 2, 4};

  EXPECT_TRUE(vec[0] == v0);
  EXPECT_TRUE(v0 == vec[0]);
  EXPECT_TRUE(vec[0] == vec[0]);
  EXPECT_FALSE(vec[0] == v1);

  EXPECT_TRUE((vec[0] <=> v0) == 0);
  EXPECT_TRUE((vec[0] <=> v1) < 0);
  EXPECT_TRUE((v1 <=> vec[0]) > 0);
  EXPECT_TRUE((vec[0] <=> vec[1]) < 0);
}

TYPED_TEST(packed_int_vec_tuple_test, field_proxy_converts_to_value) {
  using vec_type = typename TypeParam::template auto_type<trait_tuple_type>;

  vec_type vec;
  vec.push_back({small_enum::one, std::optional<uint16_t>{42}, 7});

  std::optional<uint16_t> value = get<1>(vec[0]);
  EXPECT_EQ(value, std::optional<uint16_t>{42});
}

TYPED_TEST(packed_int_vec_tuple_test, field_proxy_const_assignment) {
  using vec_type = typename TypeParam::template auto_type<trait_tuple_type>;

  vec_type vec;
  vec.push_back({small_enum::one, std::nullopt, 7});

  auto const field = get<1>(vec[0]);
  field = std::optional<uint16_t>{99};

  EXPECT_EQ(vec.get(0), (trait_tuple_type{small_enum::one,
                                          std::optional<uint16_t>{99}, 7}));
}

TYPED_TEST(packed_int_vec_tuple_test, field_proxy_assignment_from_proxy) {
  using vec_type = typename TypeParam::template auto_type<trait_tuple_type>;

  vec_type vec;
  vec.push_back({small_enum::one, std::optional<uint16_t>{11}, 7});
  vec.push_back({small_enum::big, std::optional<uint16_t>{22}, 8});

  get<1>(vec[0]) = get<1>(vec[1]);

  EXPECT_EQ(vec.get(0), (trait_tuple_type{small_enum::one,
                                          std::optional<uint16_t>{22}, 7}));
  EXPECT_EQ(vec.get(1), (trait_tuple_type{small_enum::big,
                                          std::optional<uint16_t>{22}, 8}));
}

TYPED_TEST(packed_int_vec_tuple_test, field_proxy_swap) {
  using vec_type = typename TypeParam::template auto_type<trait_tuple_type>;

  vec_type vec;
  vec.push_back({small_enum::one, std::optional<uint16_t>{11}, 7});
  vec.push_back({small_enum::big, std::optional<uint16_t>{22}, 8});

  auto a = get<1>(vec[0]);
  auto b = get<1>(vec[1]);
  using std::swap;
  swap(a, b);

  EXPECT_EQ(vec.get(0), (trait_tuple_type{small_enum::one,
                                          std::optional<uint16_t>{22}, 7}));
  EXPECT_EQ(vec.get(1), (trait_tuple_type{small_enum::big,
                                          std::optional<uint16_t>{11}, 8}));
}

TYPED_TEST(packed_int_vec_tuple_test, field_proxy_comparisons) {
  using vec_type = typename TypeParam::template auto_type<trait_tuple_type>;

  vec_type vec;
  vec.push_back({small_enum::one, std::optional<uint16_t>{11}, 7});
  vec.push_back({small_enum::big, std::optional<uint16_t>{22}, 8});
  vec.push_back({small_enum::zero, std::optional<uint16_t>{11}, 9});

  auto const eleven = std::optional<uint16_t>{11};
  auto const twenty_two = std::optional<uint16_t>{22};

  EXPECT_TRUE(get<1>(vec[0]) == eleven);
  EXPECT_TRUE(eleven == get<1>(vec[0]));
  EXPECT_TRUE(get<1>(vec[0]) == get<1>(vec[2]));
  EXPECT_FALSE(get<1>(vec[0]) == twenty_two);

  EXPECT_TRUE((get<1>(vec[0]) <=> eleven) == 0);
  EXPECT_TRUE((get<1>(vec[0]) <=> twenty_two) < 0);
  EXPECT_TRUE((twenty_two <=> get<1>(vec[0])) > 0);
  EXPECT_TRUE((get<1>(vec[0]) <=> get<1>(vec[2])) == 0);
  EXPECT_TRUE((get<1>(vec[0]) <=> get<1>(vec[1])) < 0);
}

TYPED_TEST(packed_int_vec_tuple_test, field_proxy_scalar_field_roundtrip) {
  using vec_type = typename TypeParam::template auto_type<tuple_type>;

  vec_type vec;
  vec.push_back({1, 2, 3});

  auto field = get<0>(vec[0]);
  EXPECT_EQ(static_cast<uint16_t>(field), 1);

  field = 255;
  EXPECT_EQ(vec.get(0), (tuple_type{255, 2, 3}));
}
