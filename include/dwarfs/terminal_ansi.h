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

#include <dwarfs/terminal.h>

namespace dwarfs {

class terminal_ansi : public terminal {
 public:
  enum class init_mode { AUTO, NOINIT, FORCE };

  terminal_ansi();
  terminal_ansi(init_mode mode);

  static std::string_view
  color_impl(termcolor color, termstyle style = termstyle::NORMAL);
  static std::string
  colored_impl(std::string_view text, termcolor color, bool enable = true,
               termstyle style = termstyle::NORMAL);

  size_t width() const override;
  bool is_tty(std::ostream& os) const override;
  bool is_fancy() const override;
  std::string_view color(termcolor color, termstyle style) const override;
  std::string colored(std::string text, termcolor color, bool enable,
                      termstyle style) const override;
  std::string_view carriage_return() const override;
  std::string_view rewind_line() const override;
  std::string_view clear_line() const override;
};

} // namespace dwarfs
