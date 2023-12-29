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

#include <gtest/gtest.h>

#include "dwarfs/terminal.h"

using namespace dwarfs;

TEST(terminal, ansi_color) {
  EXPECT_EQ("\033[0m", terminal_ansi_color(termcolor::NORMAL));
  EXPECT_EQ("\033[31m", terminal_ansi_color(termcolor::RED));
  EXPECT_EQ("\033[37m", terminal_ansi_color(termcolor::WHITE));
  EXPECT_EQ("\033[90m", terminal_ansi_color(termcolor::GRAY));
  EXPECT_EQ("\033[90m",
            terminal_ansi_color(termcolor::NORMAL, termstyle::BOLD));
  EXPECT_EQ("\033[1;31m", terminal_ansi_color(termcolor::BOLD_RED));
  EXPECT_EQ("\033[1;31m", terminal_ansi_color(termcolor::RED, termstyle::BOLD));
  EXPECT_EQ("\033[1;90m",
            terminal_ansi_color(termcolor::GRAY, termstyle::BOLD));
  EXPECT_EQ("\033[2;31m", terminal_ansi_color(termcolor::DIM_RED));
  EXPECT_EQ("\033[2;31m", terminal_ansi_color(termcolor::RED, termstyle::DIM));
  EXPECT_EQ("\033[2;90m", terminal_ansi_color(termcolor::GRAY, termstyle::DIM));

  auto term = terminal::create();

  EXPECT_EQ("\033[0m", term->color(termcolor::NORMAL));
  EXPECT_EQ("\033[31m", term->color(termcolor::RED));
}

TEST(terminal, ansi_colored) {
  EXPECT_EQ("\033[31mfoo\033[0m", terminal_ansi_colored("foo", termcolor::RED));
  EXPECT_EQ("foo", terminal_ansi_colored("foo", termcolor::RED, false));
  EXPECT_EQ(
      "\033[31mfoo\033[0m",
      terminal_ansi_colored("foo", termcolor::RED, true, termstyle::NORMAL));
  EXPECT_EQ(
      "\033[1;31mfoo\033[0m",
      terminal_ansi_colored("foo", termcolor::RED, true, termstyle::BOLD));
  EXPECT_EQ("\033[2;31mfoo\033[0m",
            terminal_ansi_colored("foo", termcolor::RED, true, termstyle::DIM));
  EXPECT_EQ("foo", terminal_ansi_colored("foo", termcolor::RED, false,
                                         termstyle::BOLD));

  auto term = terminal::create();

  EXPECT_EQ("\033[31mfoo\033[0m",
            term->colored("foo", termcolor::RED, true, termstyle::NORMAL));
  EXPECT_EQ("\033[1;31mfoo\033[0m",
            term->colored("foo", termcolor::RED, true, termstyle::BOLD));
  EXPECT_EQ("foo", term->colored("foo", termcolor::RED, false, termstyle::DIM));
}
