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
#include <memory>
#include <string>
#include <string_view>

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

class terminal {
 public:
  virtual ~terminal() = default;

  static std::unique_ptr<terminal const> create();
  static void setup();

  virtual size_t width() const = 0;
  virtual bool is_tty(std::ostream& os) const = 0;
  virtual bool is_fancy() const = 0;
  virtual std::string_view
  color(termcolor color, termstyle style = termstyle::NORMAL) const = 0;
  virtual std::string
  colored(std::string text, termcolor color, bool enable = true,
          termstyle style = termstyle::NORMAL) const = 0;
  virtual std::string_view carriage_return() const = 0;
  virtual std::string_view rewind_line() const = 0;
  virtual std::string_view clear_line() const = 0;
};

std::string_view
terminal_ansi_color(termcolor color, termstyle style = termstyle::NORMAL);

std::string
terminal_ansi_colored(std::string_view text, termcolor color,
                      bool enable = true, termstyle style = termstyle::NORMAL);

} // namespace dwarfs
