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

#include "test_tool_main_tester.h"

using namespace dwarfs::test;

TEST(mkdwarfs_main_test, no_cmdline_args) {
  mkdwarfs_tester t;
  auto exit_code = t.run({});
  EXPECT_EQ(exit_code, 0);
  EXPECT_TRUE(t.err().empty());
  EXPECT_FALSE(t.out().empty());
  EXPECT_THAT(t.out(), ::testing::HasSubstr("Usage: mkdwarfs"));
  EXPECT_THAT(t.out(), ::testing::HasSubstr("--help"));
}

TEST(dwarfsck_main_test, no_cmdline_args) {
  dwarfsck_tester t;
  auto exit_code = t.run({});
  EXPECT_EQ(exit_code, 0);
  EXPECT_TRUE(t.err().empty());
  EXPECT_FALSE(t.out().empty());
  EXPECT_THAT(t.out(), ::testing::HasSubstr("Usage: dwarfsck"));
  EXPECT_THAT(t.out(), ::testing::HasSubstr("--help"));
}

TEST(dwarfsextract_main_test, no_cmdline_args) {
  dwarfsextract_tester t;
  auto exit_code = t.run({});
  EXPECT_EQ(exit_code, 0);
  EXPECT_TRUE(t.err().empty());
  EXPECT_FALSE(t.out().empty());
  EXPECT_THAT(t.out(), ::testing::HasSubstr("Usage: dwarfsextract"));
  EXPECT_THAT(t.out(), ::testing::HasSubstr("--help"));
}

TEST(mkdwarfs_main_test, invalid_cmdline_args) {
  mkdwarfs_tester t;
  auto exit_code = t.run({"--some-invalid-option"});
  EXPECT_EQ(exit_code, 1);
  EXPECT_FALSE(t.err().empty());
  EXPECT_TRUE(t.out().empty());
  EXPECT_THAT(t.err(), ::testing::HasSubstr(
                           "unrecognised option '--some-invalid-option'"));
}

TEST(dwarfsck_main_test, invalid_cmdline_args) {
  dwarfsck_tester t;
  auto exit_code = t.run({"--some-invalid-option"});
  EXPECT_EQ(exit_code, 1);
  EXPECT_FALSE(t.err().empty());
  EXPECT_TRUE(t.out().empty());
  EXPECT_THAT(t.err(), ::testing::HasSubstr(
                           "unrecognised option '--some-invalid-option'"));
}

TEST(dwarfsextract_main_test, invalid_cmdline_args) {
  dwarfsextract_tester t;
  auto exit_code = t.run({"--some-invalid-option"});
  EXPECT_EQ(exit_code, 1);
  EXPECT_FALSE(t.err().empty());
  EXPECT_TRUE(t.out().empty());
  EXPECT_THAT(t.err(), ::testing::HasSubstr(
                           "unrecognised option '--some-invalid-option'"));
}

TEST(mkdwarfs_main_test, cmdline_help_arg) {
  mkdwarfs_tester t;
  auto exit_code = t.run({"--help"});
  EXPECT_EQ(exit_code, 0);
  EXPECT_TRUE(t.err().empty());
  EXPECT_FALSE(t.out().empty());
  EXPECT_THAT(t.out(), ::testing::HasSubstr("Usage: mkdwarfs"));
  EXPECT_THAT(t.out(), ::testing::HasSubstr("--help"));
  EXPECT_THAT(t.out(), ::testing::HasSubstr("--long-help"));
  // check that the detailed help is not shown
  EXPECT_THAT(t.out(),
              ::testing::Not(::testing::HasSubstr("Advanced options:")));
  EXPECT_THAT(t.out(),
              ::testing::Not(::testing::HasSubstr("Compression algorithms:")));
}

TEST(mkdwarfs_main_test, cmdline_long_help_arg) {
  mkdwarfs_tester t;
  auto exit_code = t.run({"--long-help"});
  EXPECT_EQ(exit_code, 0);
  EXPECT_TRUE(t.err().empty());
  EXPECT_FALSE(t.out().empty());
  EXPECT_THAT(t.out(), ::testing::HasSubstr("Usage: mkdwarfs"));
  EXPECT_THAT(t.out(), ::testing::HasSubstr("Advanced options:"));
  EXPECT_THAT(t.out(), ::testing::HasSubstr("Compression level defaults:"));
  EXPECT_THAT(t.out(), ::testing::HasSubstr("Compression algorithms:"));
  EXPECT_THAT(t.out(), ::testing::HasSubstr("Categories:"));
}

TEST(dwarfsck_main_test, cmdline_help_arg) {
  dwarfsck_tester t;
  auto exit_code = t.run({"--help"});
  EXPECT_EQ(exit_code, 0);
  EXPECT_TRUE(t.err().empty());
  EXPECT_FALSE(t.out().empty());
  EXPECT_THAT(t.out(), ::testing::HasSubstr("Usage: dwarfsck"));
}

TEST(dwarfsextract_main_test, cmdline_help_arg) {
  dwarfsextract_tester t;
  auto exit_code = t.run({"--help"});
  EXPECT_EQ(exit_code, 0);
  EXPECT_TRUE(t.err().empty());
  EXPECT_FALSE(t.out().empty());
  EXPECT_THAT(t.out(), ::testing::HasSubstr("Usage: dwarfsextract"));
}
