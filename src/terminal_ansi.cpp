/* vim:set ts=2 sw=2 sts=2 et: */
/**
 * \author     Marcus Holland-Moritz (github@mhxnet.de)
 * \copyright  Copyright (c) Marcus Holland-Moritz
 *
 * This file is part of dwarfs.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the “Software”), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
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

#include <dwarfs/terminal_ansi.h>

namespace dwarfs {

namespace {

constexpr size_t default_width{80};

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

size_t width_impl() {
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  if (::GetConsoleScreenBufferInfo(::GetStdHandle(STD_ERROR_HANDLE), &csbi)) {
    return csbi.srWindow.Right - csbi.srWindow.Left + 1;
  }
  return default_width;
}

bool is_tty_impl(std::ostream& os) {
  if (&os == &std::cout) {
    return ::_isatty(::_fileno(stdout));
  }
  if (&os == &std::cerr) {
    return ::_isatty(::_fileno(stderr));
  }
  return false;
}

bool is_fancy_impl() { return true; }

#else

size_t width_impl() {
  struct ::winsize w;
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
  auto rv = ::ioctl(STDERR_FILENO, TIOCGWINSZ, &w);
  return rv == 0 ? w.ws_col : default_width;
}

bool is_tty_impl(std::ostream& os) {
  if (&os == &std::cout) {
    return ::isatty(::fileno(stdout));
  }
  if (&os == &std::cerr) {
    return ::isatty(::fileno(stderr));
  }
  return false;
}

bool is_fancy_impl() {
  // TODO: we might want to use the iolayer here
  if (auto term = ::getenv("TERM")) {
    std::string_view term_sv(term);
    return !term_sv.empty() && term_sv != "dumb";
  }
  return false;
}

#endif

bool setup_impl() {
#if defined(_WIN32)
  WindowsEmulateVT100Terminal(STD_ERROR_HANDLE);
  ::SetConsoleOutputCP(CP_UTF8);
  ::SetConsoleCP(CP_UTF8);
#endif
  return true;
}

} // namespace

std::string_view terminal_ansi::color_impl(termcolor color, termstyle style) {
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

std::string terminal_ansi::colored_impl(std::string_view text, termcolor color,
                                        bool enable, termstyle style) {
  std::string result;

  if (enable) {
    auto preamble = color_impl(color, style);
    auto postamble = color_impl(termcolor::NORMAL);

    result.reserve(preamble.size() + text.size() + postamble.size());
    result.append(preamble);
    result.append(text);
    result.append(postamble);
  } else {
    result.append(text);
  }

  return result;
}

size_t terminal_ansi::width() const { return width_impl(); }

bool terminal_ansi::is_tty(std::ostream& os) const { return is_tty_impl(os); }

bool terminal_ansi::is_fancy() const { return is_fancy_impl(); }

std::string_view
terminal_ansi::color(termcolor color,
                     termstyle style = termstyle::NORMAL) const {
  return color_impl(color, style);
}

std::string
terminal_ansi::colored(std::string text, termcolor color, bool enable = true,
                       termstyle style = termstyle::NORMAL) const {
  return colored_impl(std::move(text), color, enable, style);
}

std::string_view terminal_ansi::carriage_return() const { return "\r"; }

std::string_view terminal_ansi::rewind_line() const { return "\x1b[A"; }

std::string_view terminal_ansi::clear_line() const { return "\x1b[2K"; }

terminal_ansi::terminal_ansi()
    : terminal_ansi(init_mode::AUTO) {}

terminal_ansi::terminal_ansi(init_mode mode) {
  if (mode == init_mode::AUTO) {
    static bool initialized [[maybe_unused]] = setup_impl();
  } else if (mode == init_mode::FORCE) {
    setup_impl();
  }
}

} // namespace dwarfs
