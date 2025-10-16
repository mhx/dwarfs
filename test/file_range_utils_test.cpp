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

#include <initializer_list>
#include <utility>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <dwarfs/file_range_utils.h>

namespace {

using namespace dwarfs;

using ::testing::ElementsAre;
using ::testing::IsEmpty;

::testing::Matcher<file_range> RangeIs(file_off_t off, file_size_t size) {
  return ::testing::AllOf(
      ::testing::Property(&file_range::offset, ::testing::Eq(off)),
      ::testing::Property(&file_range::size, ::testing::Eq(size)));
}

std::vector<file_range>
R(std::initializer_list<std::pair<file_off_t, file_size_t>> list) {
  std::vector<file_range> out;
  out.reserve(list.size());
  for (auto [off, sz] : list)
    out.emplace_back(off, sz);
  return out;
}

} // namespace

TEST(intersect_ranges_test, both_empty) {
  std::vector<file_range> a, b;
  auto out = intersect_ranges(a, b);
  EXPECT_THAT(out, IsEmpty());
}

TEST(intersect_ranges_test, one_empty_other_nonempty) {
  auto a = R({{0, 10}, {20, 5}});
  std::vector<file_range> b;
  EXPECT_THAT(intersect_ranges(a, b), IsEmpty());
  EXPECT_THAT(intersect_ranges(b, a), IsEmpty());
}

TEST(intersect_ranges_test, no_overlap_disjoint_far_apart) {
  auto a = R({{0, 10}, {30, 10}});
  auto b = R({{15, 5}, {50, 5}});
  EXPECT_THAT(intersect_ranges(a, b), IsEmpty());
}

TEST(intersect_ranges_test, touching_but_not_overlapping) {
  auto a = R({{0, 10}, {20, 10}});
  auto b = R({{10, 10}});
  // half-open: [0,10) ∩ [10,20) is empty; [20,30) ∩ [10,20) is empty
  EXPECT_THAT(intersect_ranges(a, b), IsEmpty());
}

TEST(intersect_ranges_test, partial_overlap_left_edge) {
  auto a = R({{0, 20}});
  auto b = R({{10, 10}});
  // overlap is [10,20)
  EXPECT_THAT(intersect_ranges(a, b), ElementsAre(RangeIs(10, 10)));
}

TEST(intersect_ranges_test, partial_overlap_right_edge) {
  auto a = R({{10, 10}});
  auto b = R({{0, 20}});
  EXPECT_THAT(intersect_ranges(a, b), ElementsAre(RangeIs(10, 10)));
}

TEST(intersect_ranges_test, one_inside_the_other_exactly) {
  auto a = R({{10, 30}}); // [10,40)
  auto b = R({{15, 10}}); // [15,25)
  EXPECT_THAT(intersect_ranges(a, b), ElementsAre(RangeIs(15, 10)));
}

TEST(intersect_ranges_test, identical_ranges) {
  auto a = R({{50, 25}});
  auto b = R({{50, 25}});
  EXPECT_THAT(intersect_ranges(a, b), ElementsAre(RangeIs(50, 25)));
}

TEST(intersect_ranges_test, multiple_overlaps_emit_multiple_segments) {
  // Classic example from the prompt
  auto a = R({{0, 20}, {50, 10}, {70, 30}});
  auto b = R({{10, 15}, {40, 40}});
  // expected: {10,10}, {50,10}, {70,10}
  EXPECT_THAT(intersect_ranges(a, b),
              ElementsAre(RangeIs(10, 10), RangeIs(50, 10), RangeIs(70, 10)));
}

TEST(intersect_ranges_test,
     staggered_ranges_create_multiple_small_intersections) {
  auto a = R({{0, 5}, {10, 5}, {20, 5}});
  auto b = R({{3, 10}, {18, 10}});
  // a[0]∩b[0] -> [3,5) = {3,2}
  // a[1]∩b[0] -> [10,13) = {10,3}
  // a[2]∩b[1] -> [20,25) = {20,5}
  EXPECT_THAT(intersect_ranges(a, b),
              ElementsAre(RangeIs(3, 2), RangeIs(10, 3), RangeIs(20, 5)));
}

TEST(intersect_ranges_test, zero_length_ranges_in_inputs_do_not_contribute) {
  auto a = R({{0, 0}, {10, 10}, {25, 0}});
  auto b = R({{5, 10}, {20, 0}});
  // Only [10,20) ∩ [5,15) -> [10,15) = {10,5}
  EXPECT_THAT(intersect_ranges(a, b), ElementsAre(RangeIs(10, 5)));
}

TEST(intersect_ranges_test, adjacency_across_multiple_segments) {
  auto a = R({{0, 10}, {20, 10}, {40, 10}});
  auto b = R({{10, 10}, {30, 10}, {50, 10}});
  // All touch but never overlap
  EXPECT_THAT(intersect_ranges(a, b), IsEmpty());
}

TEST(intersect_ranges_test, long_and_short_segments_mix) {
  auto a = R({{0, 100}});
  auto b = R({{10, 10}, {30, 5}, {50, 25}, {90, 15}});
  EXPECT_THAT(intersect_ranges(a, b),
              ElementsAre(RangeIs(10, 10), RangeIs(30, 5), RangeIs(50, 25),
                          RangeIs(90, 10)));
  // Note: last one trims to [90,100) => size 10
}

TEST(complement_ranges_test, empty_input_covers_nothing_full_file_returned) {
  file_size_t size = 100;
  auto v = std::vector<file_range>{};
  EXPECT_THAT(complement_ranges(v, size), ElementsAre(RangeIs(0, size)));
}

TEST(complement_ranges_test, zero_size_file_returns_empty_always) {
  file_size_t size = 0;
  auto v = R({{0, 0}});
  EXPECT_THAT(complement_ranges(v, size), IsEmpty());
  EXPECT_THAT(complement_ranges({}, size), IsEmpty());
}

TEST(complement_ranges_test, single_range_in_middle_yields_two_gaps) {
  file_size_t size = 100;
  auto v = R({{20, 30}}); // covers [20,50)
  EXPECT_THAT(complement_ranges(v, size),
              ElementsAre(RangeIs(0, 20), RangeIs(50, 50)));
}

TEST(complement_ranges_test, single_range_at_beginning_yields_tail_gap_only) {
  file_size_t size = 100;
  auto v = R({{0, 25}});
  EXPECT_THAT(complement_ranges(v, size), ElementsAre(RangeIs(25, 75)));
}

TEST(complement_ranges_test, single_range_at_end_yields_head_gap_only) {
  file_size_t size = 100;
  auto v = R({{60, 40}}); // [60,100)
  EXPECT_THAT(complement_ranges(v, size), ElementsAre(RangeIs(0, 60)));
}

TEST(complement_ranges_test, full_cover_single_segment_yields_empty) {
  file_size_t size = 100;
  auto v = R({{0, 100}});
  EXPECT_THAT(complement_ranges(v, size), IsEmpty());
}

TEST(complement_ranges_test, full_cover_via_multiple_adjacent_segments) {
  file_size_t size = 100;
  auto v = R({{0, 25}, {25, 50}, {75, 25}}); // perfectly adjacent coverage
  EXPECT_THAT(complement_ranges(v, size), IsEmpty());
}

TEST(complement_ranges_test, multiple_segments_leave_multiple_gaps) {
  file_size_t size = 100;
  auto v = R({{10, 10},
              {30, 5},
              {40, 10},
              {70, 10}}); // covers [10,20), [30,35), [40,50), [70,80)
  EXPECT_THAT(complement_ranges(v, size),
              ElementsAre(RangeIs(0, 10),  // head gap
                          RangeIs(20, 10), // between [10,20) and [30,35)
                          RangeIs(35, 5),  // between [30,35) and [40,50)
                          RangeIs(50, 20), // between [40,50) and [70,80)
                          RangeIs(80, 20)  // tail gap
                          ));
}

TEST(complement_ranges_test, adjacent_segments_do_not_create_zero_length_gaps) {
  file_size_t size = 60;
  auto v = R({{10, 10}, {20, 10}, {30, 10}}); // [10,40)
  EXPECT_THAT(complement_ranges(v, size),
              ElementsAre(RangeIs(0, 10), RangeIs(40, 20)));
}

TEST(complement_ranges_test, zero_length_segments_in_input_are_ignored) {
  file_size_t size = 50;
  auto v = R({{0, 0}, {10, 10}, {20, 0}, {30, 10}, {40, 0}});
  // Covered [10,20) and [30,40); gaps: [0,10), [20,30), [40,50)
  EXPECT_THAT(complement_ranges(v, size),
              ElementsAre(RangeIs(0, 10), RangeIs(20, 10), RangeIs(40, 10)));
}

TEST(complement_ranges_test, coverage_starts_at_zero_with_gap_at_end_only) {
  file_size_t size = 50;
  auto v = R({{0, 25}, {25, 5}, {30, 10}}); // covers [0,40)
  EXPECT_THAT(complement_ranges(v, size), ElementsAre(RangeIs(40, 10)));
}

TEST(complement_ranges_test, coverage_ends_at_size_with_gap_at_beginning_only) {
  file_size_t size = 50;
  auto v = R({{10, 10}, {20, 30}}); // covers [10,50)
  EXPECT_THAT(complement_ranges(v, size), ElementsAre(RangeIs(0, 10)));
}

TEST(complement_ranges_test, large_numbers_no_overflow) {
  // Ensure arithmetic near the end is correct (half-open, no overflow)
  file_size_t size = 1'000'000;
  auto v = R({{900'000, 100'000}});
  EXPECT_THAT(complement_ranges(v, size), ElementsAre(RangeIs(0, 900'000)));
}

TEST(complement_ranges_test, range_exceeds_size_throws) {
  // Ensure arithmetic near the end is correct (half-open, no overflow)
  file_size_t size = 1'000'000;
  auto v = R({{900'000, 200'000}}); // extends beyond 'size' in data?
                                    // (precondition should disallow)
  EXPECT_THAT([&]() { complement_ranges(v, size); },
              ::testing::ThrowsMessage<std::out_of_range>(
                  ::testing::HasSubstr("range exceeds size")));
}

TEST(complement_ranges_test, alternating_small_cover_leaves_many_small_gaps) {
  file_size_t size = 20;
  auto v = R({
      {1, 1},
      {3, 1},
      {5, 1},
      {7, 1},
      {9, 1},
      {11, 1},
      {13, 1},
      {15, 1},
      {17, 1},
      {19, 1},
  });
  // gaps: [0,1), [2,1), [4,1), ..., [18,1)
  EXPECT_THAT(complement_ranges(v, size),
              ElementsAre(RangeIs(0, 1), RangeIs(2, 1), RangeIs(4, 1),
                          RangeIs(6, 1), RangeIs(8, 1), RangeIs(10, 1),
                          RangeIs(12, 1), RangeIs(14, 1), RangeIs(16, 1),
                          RangeIs(18, 1)));
}
