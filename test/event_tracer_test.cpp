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

#include <sstream>
#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cpptrace/basic.hpp>

#include <dwarfs/internal/event_tracer.h>

using dwarfs::internal::event_tracer;
using dwarfs::internal::event_tracer_config;
using ::testing::HasSubstr;
using ::testing::InSequence;
using ::testing::MockFunction;

#if defined(_MSC_VER)
#define DWARFS_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define DWARFS_NOINLINE __attribute__((noinline))
#else
#define DWARFS_NOINLINE
#endif

namespace {

DWARFS_NOINLINE cpptrace::raw_trace make_trace_one() {
  return cpptrace::generate_raw_trace(0);
}

DWARFS_NOINLINE cpptrace::raw_trace make_trace_two() {
  return cpptrace::generate_raw_trace(0);
}

DWARFS_NOINLINE cpptrace::raw_trace make_trace_three() {
  return cpptrace::generate_raw_trace(0);
}

using capture_mock =
    MockFunction<cpptrace::raw_trace(std::size_t, std::size_t)>;

auto make_event_tracer(capture_mock& capture, std::size_t skip_frames = 0,
                       std::size_t max_depth = 20) -> event_tracer {
  return event_tracer(
      event_tracer_config{.skip_frames = skip_frames, .max_depth = max_depth},
      [&](std::size_t skip, std::size_t max_depth) {
        return capture.Call(skip, max_depth);
      });
}

} // namespace

TEST(event_tracer_test, dump_reports_no_events_when_empty) {
  event_tracer tracer;
  std::ostringstream os;

  tracer.dump(os, false);

  EXPECT_EQ(os.str(), "event_tracer: no events recorded\n");
}

TEST(event_tracer_test, same_trace_is_aggregated_into_one_backtrace) {
  auto trace_one = make_trace_one();

  capture_mock capture;
  auto tracer = make_event_tracer(capture);

  {
    InSequence seq;
    EXPECT_CALL(capture, Call(1, 20)).WillOnce([&] { return trace_one; });
    EXPECT_CALL(capture, Call(1, 20)).WillOnce([&] { return trace_one; });
    EXPECT_CALL(capture, Call(1, 20)).WillOnce([&] { return trace_one; });
  }

  tracer.trace("alpha");
  tracer.trace("alpha");
  tracer.trace("alpha");

  std::ostringstream os;
  tracer.dump(os, false);
  auto const output = os.str();

  EXPECT_THAT(output,
              HasSubstr("alpha: 3 hit(s) across 1 unique backtrace(s)\n"));
  EXPECT_THAT(output, HasSubstr("  3 hit(s)\n"));
}

TEST(event_tracer_test, different_traces_are_counted_as_distinct_backtraces) {
  auto trace_one = make_trace_one();
  auto trace_two = make_trace_two();

  capture_mock capture;
  auto tracer = make_event_tracer(capture);

  {
    InSequence seq;
    EXPECT_CALL(capture, Call(1, 20)).WillOnce([&] { return trace_one; });
    EXPECT_CALL(capture, Call(1, 20)).WillOnce([&] { return trace_one; });
    EXPECT_CALL(capture, Call(1, 20)).WillOnce([&] { return trace_two; });
  }

  tracer.trace("alpha");
  tracer.trace("alpha");
  tracer.trace("alpha");

  std::ostringstream os;
  tracer.dump(os, false);
  auto const output = os.str();

  EXPECT_THAT(output,
              HasSubstr("alpha: 3 hit(s) across 2 unique backtrace(s)\n"));

  auto const two_hits_pos = output.find("  2 hit(s)\n");
  auto const one_hit_pos = output.find("  1 hit(s)\n");

  ASSERT_NE(two_hits_pos, std::string::npos);
  ASSERT_NE(one_hit_pos, std::string::npos);
  EXPECT_LT(two_hits_pos, one_hit_pos);
}

TEST(event_tracer_test, different_events_are_tracked_independently) {
  auto trace_one = make_trace_one();
  auto trace_two = make_trace_two();

  capture_mock capture;
  auto tracer = make_event_tracer(capture);

  {
    InSequence seq;
    EXPECT_CALL(capture, Call(1, 20)).WillOnce([&] { return trace_one; });
    EXPECT_CALL(capture, Call(1, 20)).WillOnce([&] { return trace_one; });
    EXPECT_CALL(capture, Call(1, 20)).WillOnce([&] { return trace_two; });
  }

  tracer.trace("alpha");
  tracer.trace("alpha");
  tracer.trace("beta");

  std::ostringstream os;
  tracer.dump(os, false);
  auto const output = os.str();

  EXPECT_THAT(output,
              HasSubstr("alpha: 2 hit(s) across 1 unique backtrace(s)\n"));
  EXPECT_THAT(output,
              HasSubstr("beta: 1 hit(s) across 1 unique backtrace(s)\n"));
}

TEST(event_tracer_test, events_are_dumped_in_descending_total_count_order) {
  auto trace_one = make_trace_one();
  auto trace_two = make_trace_two();
  auto trace_three = make_trace_three();
  auto trace_four = make_trace_one();

  capture_mock capture;
  auto tracer = make_event_tracer(capture);

  {
    InSequence seq;
    EXPECT_CALL(capture, Call(1, 20)).WillOnce([&] {
      return trace_three;
    }); // beta
    EXPECT_CALL(capture, Call(1, 20)).WillOnce([&] {
      return trace_three;
    }); // beta
    EXPECT_CALL(capture, Call(1, 20)).WillOnce([&] {
      return trace_one;
    }); // alpha
    EXPECT_CALL(capture, Call(1, 20)).WillOnce([&] {
      return trace_one;
    }); // alpha
    EXPECT_CALL(capture, Call(1, 20)).WillOnce([&] {
      return trace_two;
    }); // alpha
    EXPECT_CALL(capture, Call(1, 20)).WillOnce([&] {
      return trace_four;
    }); // gamma
  }

  tracer.trace("beta");
  tracer.trace("beta");
  tracer.trace("alpha");
  tracer.trace("alpha");
  tracer.trace("alpha");
  tracer.trace("gamma");

  std::ostringstream os;
  tracer.dump(os, false);
  auto const output = os.str();

  auto const alpha_pos =
      output.find("alpha: 3 hit(s) across 2 unique backtrace(s)\n");
  auto const beta_pos =
      output.find("beta: 2 hit(s) across 1 unique backtrace(s)\n");
  auto const gamma_pos =
      output.find("gamma: 1 hit(s) across 1 unique backtrace(s)\n");

  ASSERT_NE(alpha_pos, std::string::npos);
  ASSERT_NE(beta_pos, std::string::npos);
  ASSERT_NE(gamma_pos, std::string::npos);

  EXPECT_LT(alpha_pos, beta_pos);
  EXPECT_LT(beta_pos, gamma_pos);
}

TEST(event_tracer_test, trace_passes_parameters_to_capture) {
  auto trace_one = make_trace_one();

  capture_mock capture;
  auto tracer = make_event_tracer(capture, 7, 15);

  EXPECT_CALL(capture, Call(8, 15)).WillOnce([&] { return trace_one; });

  tracer.trace("alpha");

  std::ostringstream os;
  tracer.dump(os, false);

  EXPECT_THAT(os.str(),
              HasSubstr("alpha: 1 hit(s) across 1 unique backtrace(s)\n"));
}
