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

#include <cerrno>
#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#ifndef _WIN32
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <gtest/gtest.h>

#include <dwarfs/file_util.h>
#include <dwarfs/scope_exit.h>

#include "process_guard.h"
#include "subprocess.h"

namespace dwarfs::test {

namespace {

namespace fs = std::filesystem;
using namespace std::chrono_literals;

#ifdef DWARFS_CROSSCOMPILING_EMULATOR
#define TEST_EMULATOR DWARFS_CROSSCOMPILING_EMULATOR,
#else
#define TEST_EMULATOR
#endif

fs::path const helper{SUBPROCESS_TEST_HELPER};

TEST(subprocess_test, arguments_are_not_shell_commands) {
  std::vector<std::string> args{"", "a b", "a\"b", "a b\\", "&|;$()", "\\\""};
  auto [out, err, code] = subprocess::run(TEST_EMULATOR helper, "args", args,
                                          fs::path("path with spaces"));
  EXPECT_EQ(code, 0);
  EXPECT_TRUE(err.empty());
  EXPECT_EQ(
      out,
      "0:\n3:a b\n3:a\"b\n4:a b\\\n6:&|;$()\n2:\\\"\n16:path with spaces\n");
}

TEST(subprocess_test, binary_output_and_closed_stdin) {
  auto [out, err, code] = subprocess::run(TEST_EMULATOR helper, "capture");
  EXPECT_EQ(code, 23);
  EXPECT_EQ(out, std::string(1024 * 1024, 'o') + std::string("\0\xff", 2));
  EXPECT_EQ(err, std::string(1024 * 1024, 'e'));
}

TEST(subprocess_test, checked_run) {
  auto out = subprocess::check_run(TEST_EMULATOR helper, "args");
  ASSERT_TRUE(out);
  EXPECT_TRUE(out->empty());
  EXPECT_FALSE(subprocess::check_run(TEST_EMULATOR helper, "unknown"));
}

TEST(subprocess_test, deadline_and_termination) {
  subprocess p(TEST_EMULATOR helper, "sleep");
  EXPECT_FALSE(p.wait_for(20ms));
  p.terminate();
  EXPECT_TRUE(p.wait_for(0ms));
  EXPECT_NE(p.exit_code(), 0);
}

TEST(subprocess_test, destructor_reaps_unwaited_child) {
  auto start = std::chrono::steady_clock::now();
  {
    subprocess p(TEST_EMULATOR helper, "sleep");
  }
  EXPECT_LT(std::chrono::steady_clock::now() - start, 5s);
}

TEST(subprocess_test, invalid_argument_and_missing_executable) {
  EXPECT_THROW(subprocess(helper, std::string("a\0b", 3)),
               std::invalid_argument);
  temporary_directory td("dwarfs-missing-executable");

  // Some POSIX implementations report exec failure as an exit status of 127.
  try {
    auto [out, err, code] = subprocess::run(td.path() / "does-not-exist");
    EXPECT_EQ(code, 127);
  } catch (std::system_error const&) {
    SUCCEED();
  }
}

#ifndef _WIN32
TEST(subprocess_test, signal_status) {
#ifdef DWARFS_CROSSCOMPILING_EMULATOR
  GTEST_SKIP() << "emulator signal status may differ";
#endif
  auto [out, err, code] = subprocess::run(helper, "signal");
  EXPECT_EQ(code, SIGINT);
}

TEST(subprocess_test, interrupt_direct_child) {
#ifdef DWARFS_CROSSCOMPILING_EMULATOR
  GTEST_SKIP() << "emulator signal status may differ";
#endif
  subprocess p(helper, "sleep");
  ASSERT_TRUE(p.interrupt());
  ASSERT_TRUE(p.wait_for(2s));
  EXPECT_EQ(p.exit_code(), SIGINT);
}

TEST(subprocess_test, observe_process_exit) {
  auto pid = ::fork();

  ASSERT_GE(pid, 0);

  if (pid == 0) {
    ::sleep(1);
    ::_exit(0);
  }

  auto reap = dwarfs::scope_exit([&] {
    int status;
    while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
  });

  process_guard guard(pid);
  EXPECT_FALSE(guard.wait_for(0ms));
  EXPECT_TRUE(guard.wait_for(5s));
  EXPECT_TRUE(guard.wait_for(0ms));
}

TEST(subprocess_test, inherited_output_does_not_delay_wait) {
#ifdef DWARFS_CROSSCOMPILING_EMULATOR
  GTEST_SKIP() << "emulator may manage descendants differently";
#endif
  subprocess p(helper, "inherited-output");
  ASSERT_TRUE(p.wait_for(2s));
  EXPECT_EQ(p.out(), "parent\n");
  EXPECT_EQ(p.exit_code(), 0);
}
#endif

} // namespace

} // namespace dwarfs::test
