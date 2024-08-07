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
