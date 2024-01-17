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

#include <cerrno>
#include <filesystem>

#include <fmt/format.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <folly/String.h>

#include "dwarfs/error.h"

using namespace dwarfs;

namespace {

int test_throw_runtime_error(bool throw_it) {
  if (throw_it) {
    DWARFS_THROW(runtime_error, "my test error");
  }
  return __LINE__ - 2;
}

int test_throw_system_error(bool throw_it) {
  if (throw_it) {
    errno = EPERM;
    DWARFS_THROW(system_error, "my test system error");
  }
  return __LINE__ - 2;
}

} // namespace

TEST(error_test, runtime_error) {
  int expected_line = test_throw_runtime_error(false);

  try {
    test_throw_runtime_error(true);
    FAIL() << "expected runtime_error to be thrown";
  } catch (const runtime_error& e) {
    EXPECT_EQ("error_test.cpp",
              std::filesystem::path(e.file()).filename().string());
    EXPECT_EQ(fmt::format("my test error [error_test.cpp:{}]", e.line()),
              std::string(e.what()));
    EXPECT_EQ(expected_line, e.line());
  } catch (...) {
    FAIL() << "expected runtime_error, got "
           << folly::exceptionStr(std::current_exception());
  }
}

TEST(error_test, system_error) {
  int expected_line = test_throw_system_error(false);

  try {
    test_throw_system_error(true);
    FAIL() << "expected system_error to be thrown";
  } catch (const system_error& e) {
    EXPECT_THAT(std::string(e.what()),
                ::testing::MatchesRegex("my test system error: .*"));
    EXPECT_EQ("error_test.cpp",
              std::filesystem::path(e.file()).filename().string());
    EXPECT_EQ(EPERM, e.get_errno());
    EXPECT_EQ(expected_line, e.line());
  } catch (...) {
    FAIL() << "expected system_error, got "
           << folly::exceptionStr(std::current_exception());
  }
}

TEST(error_test, dwarfs_check) {
  DWARFS_CHECK(true, "my test error");
  EXPECT_DEATH(DWARFS_CHECK(false, "my test error"), "my test error");
}

TEST(error_test, dwarfs_nothrow) {
  std::vector<int> v{1, 2, 3};
  EXPECT_EQ(3, DWARFS_NOTHROW(v.at(2)));
  EXPECT_DEATH(DWARFS_NOTHROW(v.at(3)), "Expression `v.at\\(3\\)` threw .*");
}
