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

#include <array>
#include <type_traits>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <dwarfs/internal/value_stream_quantile_estimator.h>

using dwarfs::internal::value_stream_quantile_estimator;

static_assert(!std::is_copy_constructible_v<value_stream_quantile_estimator>);
static_assert(!std::is_copy_assignable_v<value_stream_quantile_estimator>);

TEST(value_stream_quantile_estimator, quantiles) {
  auto estimator =
      value_stream_quantile_estimator({0.5, 0.75, 0.9, 0.99, 0.999});

  for (int i = 1; i <= 10000; ++i) {
    estimator.add(i);
  }

  EXPECT_NEAR(estimator.quantile(0.5), 5000.0, 1.0);
  EXPECT_NEAR(estimator.quantile(0.75), 7500.0, 1.0);
  EXPECT_NEAR(estimator.quantile(0.9), 9000.0, 1.0);
  EXPECT_NEAR(estimator.quantile(0.99), 9900.0, 1.0);
  EXPECT_NEAR(estimator.quantile(0.999), 9990.0, 1.0);
}

TEST(value_stream_quantile_estimator, is_default_constructible_and_moveable) {
  static constexpr std::array quantiles{0.99};
  value_stream_quantile_estimator estimator1(quantiles);

  for (int i = 1; i <= 500; ++i) {
    estimator1.add(i);
  }

  value_stream_quantile_estimator estimator2;

  value_stream_quantile_estimator estimator3(std::move(estimator1));
  estimator2 = std::move(estimator3);

  EXPECT_NEAR(estimator2.quantile(0.99), 495.0, 1.0);
}
