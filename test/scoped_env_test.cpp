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

#include <dwarfs/detail/scoped_env.h>

using dwarfs::detail::scoped_env;

namespace {

constexpr char const* test_var{"_DWARFS_TEST_SCOPED_ENV_"};

} // namespace

TEST(scoped_env, restores_previously_unset_variable) {
  {
    scoped_env env{test_var, "value"};
    EXPECT_STREQ("value", std::getenv(test_var));
  }
  EXPECT_EQ(nullptr, std::getenv(test_var));
}

TEST(scoped_env, restores_previous_value) {
  scoped_env outer{test_var, "outer"};
  {
    scoped_env inner{test_var, "inner"};
    EXPECT_STREQ("inner", std::getenv(test_var));
  }
  EXPECT_STREQ("outer", std::getenv(test_var));
}

TEST(scoped_env, restores_unset_variable) {
  scoped_env outer{test_var, "outer"};
  {
    scoped_env inner;
    inner.unset(test_var);
    EXPECT_EQ(nullptr, std::getenv(test_var));
  }
  EXPECT_STREQ("outer", std::getenv(test_var));
}

TEST(scoped_env, set_if_unset) {
  {
    scoped_env env;
    EXPECT_TRUE(env.set_if_unset(test_var, "value"));
    EXPECT_STREQ("value", std::getenv(test_var));
    EXPECT_FALSE(env.set_if_unset(test_var, "other"));
    EXPECT_STREQ("value", std::getenv(test_var));
  }
  EXPECT_EQ(nullptr, std::getenv(test_var));
}

TEST(scoped_env, restore_is_idempotent) {
  scoped_env env{test_var, "value"};
  env.restore();
  EXPECT_EQ(nullptr, std::getenv(test_var));
  env.restore();
  EXPECT_EQ(nullptr, std::getenv(test_var));
}
