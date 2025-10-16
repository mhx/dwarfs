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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <dwarfs/error.h>

#include <dwarfs/writer/internal/time_resolution_converter.h>

using namespace std::chrono_literals;
using namespace dwarfs;
using namespace dwarfs::writer::internal;
using testing::HasSubstr;
using testing::ThrowsMessage;

TEST(time_resolution_converter, error_handling) {
  EXPECT_THAT([] { time_resolution_converter t(1001ms); },
              ThrowsMessage<runtime_error>(HasSubstr(
                  "cannot handle resolution (1.001s) that is larger than one "
                  "second but not a whole number of seconds")));

  EXPECT_THAT([] { time_resolution_converter t(999ms); },
              ThrowsMessage<runtime_error>(
                  HasSubstr("cannot handle subsecond resolution (999ms) that "
                            "is not a whole divisor of one second")));

  EXPECT_THAT([] { time_resolution_converter t(2s, {4, 0}); },
              ThrowsMessage<runtime_error>(
                  HasSubstr("cannot convert time to a finer resolution (2s) "
                            "than the old resolution (4s)")));

  EXPECT_THAT([] { time_resolution_converter t(3s, {2, 0}); },
              ThrowsMessage<runtime_error>(HasSubstr(
                  "cannot convert time to a coarser resolution (3s) that is "
                  "not a whole multiple of the old resolution (2s)")));

  EXPECT_THAT([] { time_resolution_converter t(250ms, {1, 100'000'000}); },
              ThrowsMessage<runtime_error>(HasSubstr(
                  "cannot convert time to a coarser resolution (250ms) that is "
                  "not a whole multiple of the old resolution (100ms)")));
}

TEST(time_resolution_converter, default_conversion) {
  time_resolution_converter t(std::nullopt);

  EXPECT_TRUE(t.requires_conversion());

  auto const cf = t.new_conversion_factors();

  EXPECT_FALSE(cf.sec.has_value());
  EXPECT_FALSE(cf.nsec.has_value());

  EXPECT_EQ(42, t.convert_offset(42));
  EXPECT_EQ(0, t.convert_subsec(42));
}

TEST(time_resolution_converter, no_conversion) {
  {
    time_resolution_converter t(std::nullopt, {1, 1});

    EXPECT_FALSE(t.requires_conversion());

    auto const cf = t.new_conversion_factors();

    EXPECT_FALSE(cf.sec.has_value());
    EXPECT_EQ(1, cf.nsec);

    EXPECT_EQ(42, t.convert_offset(42));
    EXPECT_EQ(42, t.convert_subsec(42));
  }

  {
    time_resolution_converter t(5ms, {1, 5'000'000});

    EXPECT_FALSE(t.requires_conversion());

    auto const cf = t.new_conversion_factors();

    EXPECT_FALSE(cf.sec.has_value());
    EXPECT_EQ(5'000'000, cf.nsec);

    EXPECT_EQ(42, t.convert_offset(42));
    EXPECT_EQ(42, t.convert_subsec(42));
  }

  {
    time_resolution_converter t(5s, {5, 0});

    EXPECT_FALSE(t.requires_conversion());

    auto const cf = t.new_conversion_factors();

    EXPECT_EQ(5, cf.sec);
    EXPECT_FALSE(cf.nsec.has_value());

    EXPECT_EQ(42, t.convert_offset(42));
    EXPECT_EQ(42, t.convert_subsec(42));
  }
}

TEST(time_resolution_converter, convert_old_to_new) {
  {
    time_resolution_converter t(10s, {1, 100'000});

    EXPECT_TRUE(t.requires_conversion());

    auto const cf = t.new_conversion_factors();

    EXPECT_EQ(10, cf.sec);
    EXPECT_FALSE(cf.nsec.has_value());

    EXPECT_EQ(42, t.convert_offset(422));
    EXPECT_EQ(0, t.convert_subsec(1'234));
  }

  {
    time_resolution_converter t(10ms, {1, 100'000});

    EXPECT_TRUE(t.requires_conversion());

    auto const cf = t.new_conversion_factors();

    EXPECT_FALSE(cf.sec.has_value());
    EXPECT_EQ(10'000'000, cf.nsec);

    EXPECT_EQ(42, t.convert_offset(42));
    EXPECT_EQ(12, t.convert_subsec(1'234));
    EXPECT_EQ(47, t.convert_subsec(4'711));
  }
}
