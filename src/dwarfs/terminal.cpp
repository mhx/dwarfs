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

#include <folly/portability/Unistd.h>

#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include "dwarfs/terminal.h"

namespace dwarfs {

bool stream_is_fancy_terminal(std::ostream& os) {
  if (&os == &std::cout && !::isatty(::fileno(stdout))) {
    return false;
  }
  if (&os == &std::cerr && !::isatty(::fileno(stderr))) {
    return false;
  }
  auto term = ::getenv("TERM");
  return term && term[0] != '\0' && ::strcmp(term, "dumb") != 0;
}

char const* terminal_color(termcolor color) {
  static constexpr std::array<char const*,
                              static_cast<size_t>(termcolor::NUM_COLORS)>
      colors = {{
          "\033[0m",
          "\033[31m",
          "\033[32m",
          "\033[33m",
          "\033[34m",
          "\033[35m",
          "\033[36m",
          "\033[37m",
          "\033[90m",
          "\033[1;31m",
          "\033[1;32m",
          "\033[1;33m",
          "\033[1;34m",
          "\033[1;35m",
          "\033[1;36m",
          "\033[1;37m",
          "\033[1;90m",
      }};

  return colors.at(static_cast<size_t>(color));
}

std::string terminal_colored(std::string text, termcolor color, bool enable) {
  return enable
             ? terminal_color(color) + text + terminal_color(termcolor::NORMAL)
             : text;
}

} // namespace dwarfs
