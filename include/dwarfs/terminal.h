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

#pragma once

#include <iosfwd>

namespace dwarfs {

enum class termcolor {
  NORMAL,
  RED,
  GREEN,
  YELLOW,
  BLUE,
  MAGENTA,
  CYAN,
  WHITE,
  GRAY,
  BOLD_RED,
  BOLD_GREEN,
  BOLD_YELLOW,
  BOLD_BLUE,
  BOLD_MAGENTA,
  BOLD_CYAN,
  BOLD_WHITE,
  BOLD_GRAY,
  DIM_RED,
  DIM_GREEN,
  DIM_YELLOW,
  DIM_BLUE,
  DIM_MAGENTA,
  DIM_CYAN,
  DIM_WHITE,
  DIM_GRAY,
  NUM_COLORS
};

enum class termstyle { NORMAL, BOLD, DIM };

void setup_terminal();

size_t get_term_width();

bool stream_is_fancy_terminal(std::ostream& os);

char const*
terminal_color(termcolor color, termstyle style = termstyle::NORMAL);

std::string
terminal_colored(std::string text, termcolor color, bool enable = true,
                 termstyle style = termstyle::NORMAL);

} // namespace dwarfs
