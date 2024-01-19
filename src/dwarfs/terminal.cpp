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

std::string_view terminal_ansi_color(termcolor color, termstyle style) {
  static constexpr std::array<std::string_view,
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

std::string terminal_ansi_colored(std::string_view text, termcolor color,
                                  bool enable, termstyle style) {
  std::string result;

  if (enable) {
    auto preamble = terminal_ansi_color(color, style);
    auto postamble = terminal_ansi_color(termcolor::NORMAL);

    result.reserve(preamble.size() + text.size() + postamble.size());
    result.append(preamble);
    result.append(text);
    result.append(postamble);
  } else {
    result.append(text);
  }

  return result;
}

namespace {

class terminal_ansi : public terminal {
 public:
  std::string_view
  color(termcolor color, termstyle style = termstyle::NORMAL) const override {
    return terminal_ansi_color(color, style);
  }

  std::string colored(std::string text, termcolor color, bool enable = true,
                      termstyle style = termstyle::NORMAL) const override {
    return terminal_ansi_colored(std::move(text), color, enable, style);
  }

  std::string_view carriage_return() const override { return "\r"; }

  std::string_view rewind_line() const override { return "\x1b[A"; }

  std::string_view clear_line() const override { return "\x1b[2K"; }
};

#if defined(_WIN32)

void WindowsEmulateVT100Terminal(DWORD std_handle) {
  static bool done = false;

  if (done) {
    return;
  }

  done = true;

  // Enable VT processing on stdout and stdin
  auto hdl = ::GetStdHandle(std_handle);

  DWORD out_mode = 0;
  ::GetConsoleMode(hdl, &out_mode);

  // https://docs.microsoft.com/en-us/windows/console/setconsolemode
  static constexpr DWORD enable_virtual_terminal_processing = 0x0004;
  static constexpr DWORD disable_newline_auto_return = 0x0008;
  out_mode |= enable_virtual_terminal_processing;

  ::SetConsoleMode(hdl, out_mode);
}

class terminal_windows : public terminal_ansi {
 public:
  size_t width() const override {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    ::GetConsoleScreenBufferInfo(::GetStdHandle(STD_ERROR_HANDLE), &csbi);
    return csbi.srWindow.Right - csbi.srWindow.Left + 1;
  }

  bool is_tty(std::ostream& os) const override {
    if (&os == &std::cout) {
      return ::_isatty(::_fileno(stdout));
    }
    if (&os == &std::cerr) {
      return ::_isatty(::_fileno(stderr));
    }
    return false;
  }

  bool is_fancy() const override { return true; }
};

#else

class terminal_posix : public terminal_ansi {
 public:
  size_t width() const override {
    struct ::winsize w;
    ::ioctl(STDERR_FILENO, TIOCGWINSZ, &w);
    return w.ws_col;
  }

  bool is_tty(std::ostream& os) const override {
    if (&os == &std::cout) {
      return ::isatty(::fileno(stdout));
    }
    if (&os == &std::cerr) {
      return ::isatty(::fileno(stderr));
    }
    return false;
  }

  bool is_fancy() const override {
    // TODO: we might want to use the iolayer here
    if (auto term = ::getenv("TERM")) {
      std::string_view term_sv(term);
      return !term_sv.empty() && term_sv != "dumb";
    }
    return false;
  }
};

#endif

} // namespace

void terminal::setup() {
#if defined(_WIN32)
  WindowsEmulateVT100Terminal(STD_ERROR_HANDLE);
  ::SetConsoleOutputCP(CP_UTF8);
  ::SetConsoleCP(CP_UTF8);
#endif
}

std::unique_ptr<terminal const> terminal::create() {
#if defined(_WIN32)
  return std::make_unique<terminal_windows>();
#else
  return std::make_unique<terminal_posix>();
#endif
}

} // namespace dwarfs
