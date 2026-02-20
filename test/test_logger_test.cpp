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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <dwarfs/detail/scoped_env.h>

#include "test_logger.h"

using namespace dwarfs;
using namespace dwarfs::detail;
using namespace dwarfs::test;

TEST(test_logger, basic) {
  scoped_env env;
  env.unset("DWARFS_TEST_LOGGER_OUTPUT");
  env.unset("DWARFS_TEST_LOGGER_LEVEL");

  std::ostringstream oss;
  test_logger lgr(oss);

  LOG_PROXY(debug_logger_policy, lgr);

  EXPECT_TRUE(lgr.empty());
  EXPECT_EQ(logger::INFO, lgr.threshold());

  LOG_INFO << "info";
  LOG_DEBUG << "debug";

  EXPECT_FALSE(lgr.empty());

  auto const& ent = lgr.get_log();
  ASSERT_EQ(1, ent.size());
  EXPECT_EQ(logger::INFO, ent[0].level);
  EXPECT_EQ("info", ent[0].output);

  auto const str = lgr.as_string();
  EXPECT_THAT(str, testing::StartsWith("I ["));
  EXPECT_THAT(str, testing::EndsWith("] info\n"));

  {
    std::ostringstream oss2;
    oss2 << lgr;
    EXPECT_EQ(oss2.str(), str);
  }

  EXPECT_TRUE(oss.str().empty());
}

TEST(test_logger, logger_level) {
  scoped_env env;
  env.unset("DWARFS_TEST_LOGGER_OUTPUT");
  env.unset("DWARFS_TEST_LOGGER_LEVEL");

  std::ostringstream oss;
  test_logger lgr(oss, logger::DEBUG);

  LOG_PROXY(debug_logger_policy, lgr);

  EXPECT_TRUE(lgr.empty());
  EXPECT_EQ(logger::DEBUG, lgr.threshold());

  LOG_INFO << "info";
  LOG_DEBUG << "debug";

  EXPECT_FALSE(lgr.empty());

  auto const& ent = lgr.get_log();
  ASSERT_EQ(2, ent.size());
  EXPECT_EQ(logger::INFO, ent[0].level);
  EXPECT_EQ("info", ent[0].output);
  EXPECT_EQ(logger::DEBUG, ent[1].level);
  EXPECT_EQ("debug", ent[1].output);

  auto const str = lgr.as_string();
  EXPECT_THAT(str, testing::StartsWith("I ["));
  EXPECT_THAT(str, testing::HasSubstr("] info\nD "));
  EXPECT_THAT(str, testing::EndsWith("] debug\n"));

  {
    std::ostringstream oss2;
    oss2 << lgr;
    EXPECT_EQ(oss2.str(), str);
  }

  EXPECT_TRUE(oss.str().empty());
}

TEST(test_logger, logger_output) {
  scoped_env env;
  env.unset("DWARFS_TEST_LOGGER_LEVEL");
  env.set("DWARFS_TEST_LOGGER_OUTPUT", "1");

  std::ostringstream oss;
  test_logger lgr(oss);

  LOG_PROXY(debug_logger_policy, lgr);

  EXPECT_TRUE(lgr.empty());
  EXPECT_EQ(logger::INFO, lgr.threshold());

  LOG_INFO << "info";
  LOG_DEBUG << "debug";

  auto const& ent = lgr.get_log();
  ASSERT_EQ(1, ent.size());
  EXPECT_EQ(logger::INFO, ent[0].level);
  EXPECT_EQ("info", ent[0].output);

  auto const str = oss.str();
  EXPECT_THAT(str, testing::StartsWith("I "));
  EXPECT_THAT(str, testing::HasSubstr(__FILE__));
  EXPECT_THAT(str, testing::EndsWith("] info\n"));
}

TEST(test_logger, logger_output_level) {
  scoped_env env;
  env.set("DWARFS_TEST_LOGGER_LEVEL", "debug");
  env.set("DWARFS_TEST_LOGGER_OUTPUT", "1");

  std::ostringstream oss;
  test_logger lgr(oss);

  LOG_PROXY(debug_logger_policy, lgr);

  EXPECT_TRUE(lgr.empty());
  EXPECT_EQ(logger::DEBUG, lgr.threshold());

  LOG_INFO << "info";
  LOG_DEBUG << "debug";

  auto const& ent = lgr.get_log();
  ASSERT_EQ(1, ent.size());
  EXPECT_EQ(logger::INFO, ent[0].level);
  EXPECT_EQ("info", ent[0].output);

  auto const str = oss.str();
  EXPECT_THAT(str, testing::StartsWith("I "));
  EXPECT_THAT(str, testing::HasSubstr(__FILE__));
  EXPECT_THAT(str, testing::HasSubstr("] info\nD "));
  EXPECT_THAT(str, testing::EndsWith("] debug\n"));
}
