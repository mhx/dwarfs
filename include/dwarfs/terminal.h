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

#pragma once

#include <iosfwd>
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

} // namespace dwarfs
