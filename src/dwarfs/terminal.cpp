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

#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>

#ifndef _WIN32
#include <sys/ioctl.h>
#endif

#include <folly/portability/Unistd.h>
#include <folly/portability/Windows.h>

#include "dwarfs/terminal.h"

namespace dwarfs {

#if defined(_WIN32)
void WindowsEmulateVT100Terminal(DWORD std_handle) {
  static bool done = false;

  if (done) {
    return;
  }

  done = true;

  // Enable VT processing on stdout and stdin
  auto hdl = ::GetStdHandle(STD_OUTPUT_HANDLE);

  DWORD out_mode = 0;
  ::GetConsoleMode(hdl, &out_mode);

  // https://docs.microsoft.com/en-us/windows/console/setconsolemode
  static constexpr DWORD enable_virtual_terminal_processing = 0x0004;
  static constexpr DWORD disable_newline_auto_return = 0x0008;
  out_mode |= enable_virtual_terminal_processing;

  ::SetConsoleMode(hdl, out_mode);
}
#endif

void setup_terminal() {
#ifdef _WIN32
  WindowsEmulateVT100Terminal(STD_OUTPUT_HANDLE);
  ::SetConsoleOutputCP(CP_UTF8);
  ::SetConsoleCP(CP_UTF8);
#endif
}

size_t get_term_width() {
#ifdef _WIN32
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  ::GetConsoleScreenBufferInfo(::GetStdHandle(STD_ERROR_HANDLE), &csbi);
  return csbi.srWindow.Right - csbi.srWindow.Left + 1;
#else
  struct ::winsize w;
  ::ioctl(STDERR_FILENO, TIOCGWINSZ, &w);
  return w.ws_col;
#endif
}

bool stream_is_fancy_terminal(std::ostream& os [[maybe_unused]]) {
#ifdef _WIN32
  if (&os == &std::cout) {
    return true;
  }
  if (&os == &std::cerr) {
    return true;
  }
  return false;
#else
  if (&os == &std::cout && !::isatty(::fileno(stdout))) {
    return false;
  }
  if (&os == &std::cerr && !::isatty(::fileno(stderr))) {
    return false;
  }
  auto term = ::getenv("TERM");
  return term && term[0] != '\0' && ::strcmp(term, "dumb") != 0;
#endif
}

char const* terminal_color(termcolor color, termstyle style) {
  static constexpr std::array<char const*,
                              static_cast<size_t>(termcolor::NUM_COLORS)>
      // clang-format off
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
          "\033[2;31m",
          "\033[2;32m",
          "\033[2;33m",
          "\033[2;34m",
          "\033[2;35m",
          "\033[2;36m",
          "\033[2;37m",
          "\033[2;90m",
      }};
  // clang-format on

  static constexpr size_t const kBoldOffset{
      static_cast<size_t>(termcolor::BOLD_RED) -
      static_cast<size_t>(termcolor::RED)};
  static constexpr size_t const kDimOffset{
      static_cast<size_t>(termcolor::DIM_RED) -
      static_cast<size_t>(termcolor::RED)};

  switch (style) {
  case termstyle::BOLD:
  case termstyle::DIM: {
    auto ix = static_cast<size_t>(color);
    if (ix < static_cast<size_t>(termcolor::BOLD_RED)) {
      color = static_cast<termcolor>(
          ix + (style == termstyle::BOLD ? kBoldOffset : kDimOffset));
    }
  } break;

  default:
    break;
  }

  return colors.at(static_cast<size_t>(color));
}

std::string terminal_colored(std::string text, termcolor color, bool enable,
                             termstyle style) {
  return enable ? terminal_color(color, style) + text +
                      terminal_color(termcolor::NORMAL)
                : text;
}

} // namespace dwarfs
