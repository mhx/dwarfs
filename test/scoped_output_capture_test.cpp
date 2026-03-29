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

  EXPECT_THAT(cap.captured(), Eq("hello from printf"));
}

TEST(scoped_output_capture_test, captures_stdout_via_iostream) {
  scoped_output_capture cap;

  std::cout << "hello from iostream" << std::flush;

  EXPECT_THAT(cap.captured(), Eq("hello from iostream"));
}

TEST(scoped_output_capture_test, captures_stderr_via_fprintf) {
  scoped_output_capture cap{scoped_output_capture::stream::std_err};

  ASSERT_GE(std::fprintf(stderr, "%s", "hello from fprintf"), 0);
  std::fflush(stderr);

  EXPECT_THAT(cap.captured(), Eq("hello from fprintf"));
}

TEST(scoped_output_capture_test, captures_stdout_and_stderr_independently) {
  scoped_output_capture cap_out{scoped_output_capture::stream::std_out};
  scoped_output_capture cap_err{scoped_output_capture::stream::std_err};

  ASSERT_GE(std::printf("stdout text"), 0);
  std::fflush(stdout);
  ASSERT_GE(std::fprintf(stderr, "%s", "stderr text"), 0);
  std::fflush(stderr);

  EXPECT_THAT(cap_out.captured(), Eq("stdout text"));
  EXPECT_THAT(cap_err.captured(), Eq("stderr text"));
}

TEST(scoped_output_capture_test, captures_stdout_via_file_descriptor_api) {
  scoped_output_capture cap;

  write_stdout_fd("fd stdout");

  EXPECT_THAT(cap.captured(), Eq("fd stdout"));
}

TEST(scoped_output_capture_test, captures_stderr_via_file_descriptor_api) {
  scoped_output_capture cap{scoped_output_capture::stream::std_err};

  write_stderr_fd("fd stderr");

  EXPECT_THAT(cap.captured(), Eq("fd stderr"));
}

#ifdef _WIN32
TEST(scoped_output_capture_test, captures_stdout_via_writefile_getstdhandle) {
  scoped_output_capture cap;

  write_stdout_handle("handle stdout");

  EXPECT_THAT(cap.captured(), Eq("handle stdout"));
}

TEST(scoped_output_capture_test, captures_stderr_via_writefile_getstdhandle) {
  scoped_output_capture cap{scoped_output_capture::stream::std_err};

  write_stderr_handle("handle stderr");

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

  EXPECT_THAT(first.captured(), Eq("before stop"));
  EXPECT_THAT(second.captured(), Eq("after stop"));
}

TEST(scoped_output_capture_test, move_constructor_preserves_active_capture) {
  scoped_output_capture original;
  write_stdout_fd("part one ");

  scoped_output_capture moved{std::move(original)};
  write_stdout_fd("part two");

  EXPECT_THAT(moved.captured(), Eq("part one part two"));
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

  EXPECT_THAT(cap.captured(), Eq(expected));
}

TEST(scoped_output_capture_test,
     inner_capture_does_not_tee_output_to_outer_capture) {
  scoped_output_capture outer;

  {
    scoped_output_capture inner;
    write_stdout_fd("inner only");
    EXPECT_THAT(inner.captured(), Eq("inner only"));
  }

  EXPECT_THAT(outer.captured(), Eq(""));
}

TEST(scoped_output_capture_test, destructor_restores_previous_capture) {
  scoped_output_capture outer;

  {
    scoped_output_capture inner;
    write_stdout_fd("inner");
  }

  write_stdout_fd("outer");

  EXPECT_THAT(outer.captured(), Eq("outer"));
}

} // namespace
