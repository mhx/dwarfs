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

#include <limits>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <dwarfs/internal/detail/align_advise_range.h>

namespace {

using namespace dwarfs;

using dwarfs::internal::detail::advise_range_constraints;
using dwarfs::internal::detail::align_advise_range;

inline void expect_ok(std::error_code const& ec) {
  EXPECT_FALSE(ec) << "unexpected error: " << ec.message();
}

inline void expect_invalid_argument(std::error_code const& ec) {
  EXPECT_TRUE(ec) << "expected an error";
  EXPECT_EQ(ec, std::make_error_code(std::errc::invalid_argument))
      << "expected invalid_argument";
}

inline void expect_page_aligned(size_t x, size_t gran) {
  EXPECT_EQ(x % gran, 0U);
}

constexpr size_t kGranularity{4096};

} // anonymous namespace

TEST(align_advise_range, include_partial_aligned_start_keeps_unaligned_length) {
  static constexpr auto kConstraints = advise_range_constraints{
      .page_offset = 0,
      .mapped_size = 2 * kGranularity,
      .granularity = kGranularity,
  };

  std::error_code ec;
  auto const r = align_advise_range({0, 2000}, kConstraints,
                                    io_advice_range::include_partial, ec);

  expect_ok(ec);
  EXPECT_EQ(r.offset, 0U);
  EXPECT_EQ(r.size, 2000U);
  expect_page_aligned(r.offset, kGranularity);
}

TEST(align_advise_range, include_partial_misaligned_start_expands_backward) {
  static constexpr auto kConstraints = advise_range_constraints{
      .page_offset = 123,
      .mapped_size = 2 * kGranularity - 100,
      .granularity = kGranularity,
  };

  std::error_code ec;
  auto const r = align_advise_range({0, 100}, kConstraints,
                                    io_advice_range::include_partial, ec);

  expect_ok(ec);
  EXPECT_EQ(r.offset, 0U);
  EXPECT_EQ(r.size, 223U);
  expect_page_aligned(r.offset, kGranularity);
}

TEST(align_advise_range, include_partial_crosses_page_boundary) {
  static constexpr auto kConstraints = advise_range_constraints{
      .page_offset = 3500,
      .mapped_size = 2 * kGranularity - 100,
      .granularity = kGranularity,
  };

  std::error_code ec;
  auto const r = align_advise_range({0, 1000}, kConstraints,
                                    io_advice_range::include_partial, ec);

  expect_ok(ec);
  EXPECT_EQ(r.offset, 0U);
  EXPECT_EQ(r.size, 4500U);
  expect_page_aligned(r.offset, kGranularity);
}

TEST(align_advise_range, include_partial_tail_exactly_at_mapping_end) {
  static constexpr auto kConstraints = advise_range_constraints{
      .page_offset = 0,
      .mapped_size = 2 * kGranularity - 100,
      .granularity = kGranularity,
  };

  std::error_code ec;
  auto const r =
      align_advise_range({1000, 2 * kGranularity - 1000}, kConstraints,
                         io_advice_range::include_partial, ec);

  expect_ok(ec);
  EXPECT_EQ(r.offset, 0U);
  EXPECT_EQ(r.size, 2 * kGranularity - 100);
  expect_page_aligned(r.offset, kGranularity);
}

TEST(align_advise_range, exclude_partial_basic_one_page) {
  static constexpr auto kConstraints = advise_range_constraints{
      .page_offset = 0,
      .mapped_size = 2 * kGranularity - 100,
      .granularity = kGranularity,
  };

  std::error_code ec;
  auto const r = align_advise_range({0, 5000}, kConstraints,
                                    io_advice_range::exclude_partial, ec);

  expect_ok(ec);
  EXPECT_EQ(r.offset, 0U);
  EXPECT_EQ(r.size, kGranularity);
  expect_page_aligned(r.offset, kGranularity);
  EXPECT_EQ(r.size % kGranularity, 0U);
}

TEST(align_advise_range, exclude_partial_misaligned_small_range_becomes_empty) {
  static constexpr auto kConstraints = advise_range_constraints{
      .page_offset = 100,
      .mapped_size = 2 * kGranularity - 100,
      .granularity = kGranularity,
  };

  std::error_code ec;
  auto const r = align_advise_range({0, 50}, kConstraints,
                                    io_advice_range::exclude_partial, ec);

  expect_ok(ec);
  EXPECT_EQ(r.size, 0U);
  expect_page_aligned(r.offset, kGranularity);
}

TEST(align_advise_range, exclude_partial_crossing_pages_trims_to_full_pages) {
  static constexpr auto kConstraints = advise_range_constraints{
      .page_offset = 3500,
      .mapped_size = 2 * kGranularity,
      .granularity = kGranularity,
  };

  std::error_code ec{};
  auto const r = align_advise_range({0, 5000}, kConstraints,
                                    io_advice_range::exclude_partial, ec);

  expect_ok(ec);
  EXPECT_EQ(r.offset, kGranularity);
  EXPECT_EQ(r.size, kGranularity);
  expect_page_aligned(r.offset, kGranularity);
  EXPECT_EQ(r.size % kGranularity, 0U);
}

TEST(align_advise_range, exclude_partial_exact_page_aligned_span_unchanged) {
  static constexpr auto kConstraints = advise_range_constraints{
      .page_offset = 0,
      .mapped_size = 3 * kGranularity,
      .granularity = kGranularity,
  };

  std::error_code ec;
  auto const r = align_advise_range({kGranularity, kGranularity}, kConstraints,
                                    io_advice_range::exclude_partial, ec);

  expect_ok(ec);
  EXPECT_EQ(r.offset, kGranularity);
  EXPECT_EQ(r.size, kGranularity);
  expect_page_aligned(r.offset, kGranularity);
  EXPECT_EQ(r.size % kGranularity, 0U);
}

TEST(align_advise_range, exclude_partial_zero_size_stays_empty) {
  static constexpr auto kConstraints = advise_range_constraints{
      .page_offset = 100,
      .mapped_size = 2 * kGranularity,
      .granularity = kGranularity,
  };

  std::error_code ec;
  auto const r = align_advise_range({0, 0}, kConstraints,
                                    io_advice_range::exclude_partial, ec);

  expect_ok(ec);
  EXPECT_EQ(r.size, 0U);
  expect_page_aligned(r.offset, kGranularity);
}

TEST(align_advise_range, include_partial_tail_cannot_exceed_mapping) {
  static constexpr auto kConstraints = advise_range_constraints{
      .page_offset = 3000,
      .mapped_size = 2 * kGranularity,
      .granularity = kGranularity,
  };

  std::error_code ec;
  auto const r = align_advise_range({0, 10000}, kConstraints,
                                    io_advice_range::include_partial, ec);

  expect_ok(ec);
  EXPECT_EQ(r.offset, 0U);
  EXPECT_EQ(r.size, 2 * kGranularity);
  expect_page_aligned(r.offset, kGranularity);
}

TEST(align_advise_range, error_granularity_zero) {
  static constexpr auto kConstraints = advise_range_constraints{
      .page_offset = 0,
      .mapped_size = 2 * kGranularity,
      .granularity = 0,
  };

  std::error_code ec;
  auto const r = align_advise_range({0, 100}, kConstraints,
                                    io_advice_range::include_partial, ec);

  (void)r;
  expect_invalid_argument(ec);
}

TEST(align_advise_range, error_page_offset_not_less_than_granularity) {
  static constexpr auto kConstraints = advise_range_constraints{
      .page_offset = kGranularity,
      .mapped_size = 2 * kGranularity,
      .granularity = kGranularity,
  };

  std::error_code ec;
  auto const r = align_advise_range({0, 10}, kConstraints,
                                    io_advice_range::include_partial, ec);

  (void)r;
  expect_invalid_argument(ec);
}

TEST(align_advise_range, error_offset_plus_page_offset_exceeds_mapping) {
  static constexpr auto kConstraints = advise_range_constraints{
      .page_offset = 100,
      .mapped_size = 1000,
      .granularity = kGranularity,
  };

  std::error_code ec;
  auto const r = align_advise_range({901, 10}, kConstraints,
                                    io_advice_range::include_partial, ec);
  (void)r;
  expect_invalid_argument(ec);
}

TEST(align_advise_range, error_offset_overflow_is_rejected) {
  static constexpr auto kConstraints = advise_range_constraints{
      .page_offset = 123,
      .mapped_size = 2 * kGranularity,
      .granularity = kGranularity,
  };

  std::error_code ec;
  auto const r =
      align_advise_range({std::numeric_limits<size_t>::max() - 100, 10},
                         kConstraints, io_advice_range::include_partial, ec);
  (void)r;
  expect_invalid_argument(ec);
}

TEST(align_advise_range, exclude_partial_rounds_both_ends_to_page) {
  static constexpr auto kConstraints = advise_range_constraints{
      .page_offset = 123,
      .mapped_size = 3 * kGranularity,
      .granularity = kGranularity,
  };

  std::error_code ec;
  auto const r = align_advise_range({100, 6000}, kConstraints,
                                    io_advice_range::exclude_partial, ec);

  expect_ok(ec);
  EXPECT_EQ(r.offset, kGranularity);
  EXPECT_EQ(r.size, 0U);
  expect_page_aligned(r.offset, kGranularity);
}

TEST(align_advise_range, exclude_partial_large_span_multiple_pages) {
  static constexpr auto kConstraints = advise_range_constraints{
      .page_offset = 500,
      .mapped_size = 5 * kGranularity,
      .granularity = kGranularity,
  };

  std::error_code ec;
  auto const r =
      align_advise_range({200, 4 * kGranularity + 1200}, kConstraints,
                         io_advice_range::exclude_partial, ec);

  expect_ok(ec);
  EXPECT_EQ(r.offset, kGranularity);
  EXPECT_EQ(r.size, 3 * kGranularity);
  expect_page_aligned(r.offset, kGranularity);
  EXPECT_EQ(r.size % kGranularity, 0U);
}

TEST(align_advise_range, include_partial_respects_mapping_upper_bound) {
  static constexpr auto kConstraints = advise_range_constraints{
      .page_offset = 0,
      .mapped_size = kGranularity,
      .granularity = kGranularity,
  };

  std::error_code ec;
  auto const r = align_advise_range({kGranularity - 100, 5000}, kConstraints,
                                    io_advice_range::include_partial, ec);

  expect_ok(ec);
  EXPECT_EQ(r.offset, 0U);
  EXPECT_EQ(r.size, kGranularity);
  expect_page_aligned(r.offset, kGranularity);
}

TEST(align_advise_range,
     exclude_partial_when_rounded_start_past_mapping_returns_empty) {
  static constexpr auto kConstraints = advise_range_constraints{
      .page_offset = kGranularity - 1,
      .mapped_size = kGranularity,
      .granularity = kGranularity,
  };

  std::error_code ec;
  auto const r = align_advise_range({1, 1}, kConstraints,
                                    io_advice_range::exclude_partial, ec);

  expect_ok(ec);
  EXPECT_EQ(r.offset, kGranularity);
  EXPECT_EQ(r.size, 0U);
  expect_page_aligned(r.offset, kGranularity);
}

TEST(align_advise_range,
     exclude_partial_when_rounded_start_past_mapping_returns_empty_2) {
  static constexpr auto kConstraints = advise_range_constraints{
      .page_offset = kGranularity - 100,
      .mapped_size = kGranularity + 100,
      .granularity = kGranularity,
  };

  std::error_code ec;
  auto const r = align_advise_range({150, 10}, kConstraints,
                                    io_advice_range::exclude_partial, ec);

  expect_ok(ec);
  EXPECT_EQ(r.offset, 0U);
  EXPECT_EQ(r.size, 0U);
}
