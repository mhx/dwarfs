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

#include <cstdio>
#include <cstring>
#include <string>
#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#ifdef _WIN32
#include <dwarfs/portability/windows.h>
#else
#include <unistd.h>
#endif

#include <dwarfs/scoped_output_capture.h>

namespace {

using ::testing::Eq;
using ::testing::ThrowsMessage;

using dwarfs::scoped_output_capture;

void write_stdout_fd(std::string const& text) {
#ifdef _WIN32
  ASSERT_NE(_fileno(stdout), -1);
  ASSERT_NE(
      _write(_fileno(stdout), text.data(), static_cast<unsigned>(text.size())),
      -1);
#else
  ASSERT_NE(::write(STDOUT_FILENO, text.data(), text.size()), -1);
#endif
}

void write_stderr_fd(std::string const& text) {
#ifdef _WIN32
  ASSERT_NE(_fileno(stderr), -1);
  ASSERT_NE(
      _write(_fileno(stderr), text.data(), static_cast<unsigned>(text.size())),
      -1);
#else
  ASSERT_NE(::write(STDERR_FILENO, text.data(), text.size()), -1);
#endif
}

#ifdef _WIN32
void write_stdout_handle(std::string const& text) {
  HANDLE handle = ::GetStdHandle(STD_OUTPUT_HANDLE);
  ASSERT_NE(handle, INVALID_HANDLE_VALUE);
  ASSERT_NE(handle, nullptr);

  DWORD written = 0;
  ASSERT_TRUE(::WriteFile(handle, text.data(), static_cast<DWORD>(text.size()),
                          &written, nullptr));
  ASSERT_EQ(written, text.size());
}

void write_stderr_handle(std::string const& text) {
  HANDLE handle = ::GetStdHandle(STD_ERROR_HANDLE);
  ASSERT_NE(handle, INVALID_HANDLE_VALUE);
  ASSERT_NE(handle, nullptr);

  DWORD written = 0;
  ASSERT_TRUE(::WriteFile(handle, text.data(), static_cast<DWORD>(text.size()),
                          &written, nullptr));
  ASSERT_EQ(written, text.size());
}
#endif

TEST(scoped_output_capture_test,
     captures_stdout_via_printf_using_captured_alias) {
  scoped_output_capture cap;

  ASSERT_GE(std::printf("hello from printf"), 0);
  std::fflush(stdout);

  EXPECT_THAT(cap.captured_stdout(), Eq("hello from printf"));
  EXPECT_THAT(cap.captured(), Eq("hello from printf"));
}

TEST(scoped_output_capture_test, captures_stdout_via_iostream) {
  scoped_output_capture cap;

  std::cout << "hello from iostream" << std::flush;

  EXPECT_THAT(cap.captured_stdout(), Eq("hello from iostream"));
  EXPECT_THAT(cap.captured(), Eq("hello from iostream"));
}

TEST(scoped_output_capture_test, captures_stderr_via_fprintf) {
  scoped_output_capture cap{scoped_output_capture::capture::stderr_only};

  ASSERT_GE(std::fprintf(stderr, "%s", "hello from fprintf"), 0);
  std::fflush(stderr);

  EXPECT_THAT(cap.captured_stderr(), Eq("hello from fprintf"));
  EXPECT_THAT(cap.captured(), Eq("hello from fprintf"));
}

TEST(scoped_output_capture_test, captures_stdout_and_stderr_independently) {
  scoped_output_capture cap{scoped_output_capture::capture::both};

  ASSERT_GE(std::printf("stdout text"), 0);
  std::fflush(stdout);
  ASSERT_GE(std::fprintf(stderr, "%s", "stderr text"), 0);
  std::fflush(stderr);

  EXPECT_THAT(cap.captured_stdout(), Eq("stdout text"));
  EXPECT_THAT(cap.captured_stderr(), Eq("stderr text"));
}

TEST(scoped_output_capture_test, captures_stdout_via_file_descriptor_api) {
  scoped_output_capture cap;

  write_stdout_fd("fd stdout");

  EXPECT_THAT(cap.captured_stdout(), Eq("fd stdout"));
  EXPECT_THAT(cap.captured(), Eq("fd stdout"));
}

TEST(scoped_output_capture_test, captures_stderr_via_file_descriptor_api) {
  scoped_output_capture cap{scoped_output_capture::capture::stderr_only};

  write_stderr_fd("fd stderr");

  EXPECT_THAT(cap.captured_stderr(), Eq("fd stderr"));
  EXPECT_THAT(cap.captured(), Eq("fd stderr"));
}

#ifdef _WIN32
TEST(scoped_output_capture_test, captures_stdout_via_writefile_getstdhandle) {
  scoped_output_capture cap;

  write_stdout_handle("handle stdout");

  EXPECT_THAT(cap.captured_stdout(), Eq("handle stdout"));
  EXPECT_THAT(cap.captured(), Eq("handle stdout"));
}

TEST(scoped_output_capture_test, captures_stderr_via_writefile_getstdhandle) {
  scoped_output_capture cap{scoped_output_capture::capture::stderr_only};

  write_stderr_handle("handle stderr");

  EXPECT_THAT(cap.captured_stderr(), Eq("handle stderr"));
  EXPECT_THAT(cap.captured(), Eq("handle stderr"));
}
#endif

TEST(scoped_output_capture_test,
     stop_is_idempotent_and_freezes_the_first_capture) {
  scoped_output_capture first;

  write_stdout_fd("before stop");
  ASSERT_NO_THROW(first.stop());
  ASSERT_NO_THROW(first.stop());

  scoped_output_capture second;
  write_stdout_fd("after stop");

  EXPECT_THAT(first.captured_stdout(), Eq("before stop"));
  EXPECT_THAT(second.captured_stdout(), Eq("after stop"));
}

TEST(scoped_output_capture_test, move_constructor_preserves_active_capture) {
  scoped_output_capture original;
  write_stdout_fd("part one ");

  scoped_output_capture moved{std::move(original)};
  write_stdout_fd("part two");

  EXPECT_THAT(moved.captured_stdout(), Eq("part one part two"));
}

TEST(scoped_output_capture_test,
     writing_more_than_pipe_capacity_is_fully_captured) {
  constexpr std::size_t repeats = 32 * 1024;
  std::string expected;
  expected.reserve(repeats);

  for (std::size_t i = 0; i != repeats; ++i) {
    expected.push_back(static_cast<char>('a' + (i % 26)));
  }

  scoped_output_capture cap;
  write_stdout_fd(expected);

  EXPECT_THAT(cap.captured_stdout(), Eq(expected));
}

TEST(scoped_output_capture_test,
     requesting_an_uncaptured_stream_throws_logic_error) {
  scoped_output_capture stderr_only{
      scoped_output_capture::capture::stderr_only};
  scoped_output_capture stdout_only{
      scoped_output_capture::capture::stdout_only};
  scoped_output_capture cap_both{scoped_output_capture::capture::both};

  EXPECT_THROW(stderr_only.captured_stdout(), std::logic_error);
  EXPECT_THROW(stdout_only.captured_stderr(), std::logic_error);
  EXPECT_THROW(cap_both.captured(), std::logic_error);
}

TEST(scoped_output_capture_test,
     inner_capture_does_not_tee_output_to_outer_capture) {
  scoped_output_capture outer;

  {
    scoped_output_capture inner;
    write_stdout_fd("inner only");
    EXPECT_THAT(inner.captured_stdout(), Eq("inner only"));
  }

  EXPECT_THAT(outer.captured_stdout(), Eq(""));
}

TEST(scoped_output_capture_test, destructor_restores_previous_capture) {
  scoped_output_capture outer;

  {
    scoped_output_capture inner;
    write_stdout_fd("inner");
  }

  write_stdout_fd("outer");

  EXPECT_THAT(outer.captured_stdout(), Eq("outer"));
}

} // namespace
