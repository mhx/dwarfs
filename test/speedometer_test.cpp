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
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "dwarfs/speedometer.h"

using namespace dwarfs;

namespace {

class mock_clock {
 public:
  using duration = std::chrono::microseconds;
  using time_point = std::chrono::time_point<mock_clock>;

  static time_point now() { return now_; }

  static void advance(duration d) { now_ += d; }

 private:
  static thread_local inline time_point now_;
};

template <typename T>
using test_speedometer = basic_speedometer<mock_clock, T>;

} // namespace

TEST(speedometer_test, basic) {
  using namespace std::chrono_literals;

  test_speedometer<int> s{2s};

  EXPECT_EQ(0, s.num_per_second());

  mock_clock::advance(250ms);
  s.put(10000);

  EXPECT_EQ(0, s.num_per_second());

  mock_clock::advance(250ms); // [250ms, 500ms]
  s.put(20000);               // [10000 -> 20000]

  EXPECT_EQ(40000, s.num_per_second());

  mock_clock::advance(750ms); // [250ms, 1250ms]
  s.put(90000);               // [10000 -> 90000]

  EXPECT_EQ(80000, s.num_per_second());

  mock_clock::advance(500ms); // [250ms, 1750ms]
  s.put(115000);              // [10000 -> 115000]

  EXPECT_EQ(70000, s.num_per_second());

  mock_clock::advance(500ms); // [250ms, 2250ms]
  s.put(130000);              // [10000 -> 130000]

  EXPECT_EQ(60000, s.num_per_second());

  mock_clock::advance(500ms); // [1250ms, 2750ms]
  s.put(150000);              // [90000 -> 150000]

  EXPECT_EQ(40000, s.num_per_second());

  mock_clock::advance(750ms); // [1750ms, 3500ms]
  s.put(176250);              // [115000 -> 176250]

  EXPECT_EQ(35000, s.num_per_second());
}
