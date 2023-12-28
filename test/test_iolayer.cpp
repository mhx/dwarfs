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

#include <fmt/format.h>

#include "test_helpers.h"

namespace dwarfs::test {

test_terminal::test_terminal(std::ostream& out, std::ostream& err)
    : out_{&out}
    , err_{&err} {}

size_t test_terminal::width() const { return width_; }

bool test_terminal::is_fancy(std::ostream& os) const {
  return fancy_ && (&os == out_ || &os == err_);
}

std::string_view test_terminal::color(termcolor color, termstyle style) const {
  static constexpr std::array<std::string_view,
                              static_cast<size_t>(termcolor::NUM_COLORS)>
      // clang-format off
      colors = {{
          "<normal>",
          "<red>",
          "<green>",
          "<yellow>",
          "<blue>",
          "<magenta>",
          "<cyan>",
          "<white>",
          "<gray>",
          "<bold-red>",
          "<bold-green>",
          "<bold-yellow>",
          "<bold-blue>",
          "<bold-magenta>",
          "<bold-cyan>",
          "<bold-white>",
          "<bold-gray>",
          "<dim-red>",
          "<dim-green>",
          "<dim-yellow>",
          "<dim-blue>",
          "<dim-magenta>",
          "<dim-cyan>",
          "<dim-white>",
          "<dim-gray>",
      }}; // clang-format on

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

std::string test_terminal::colored(std::string text, termcolor color,
                                   bool enable, termstyle style) const {
  std::string result;

  if (enable) {
    auto preamble = this->color(color, style);
    auto postamble = this->color(termcolor::NORMAL, termstyle::NORMAL);

    result.reserve(preamble.size() + text.size() + postamble.size());
    result.append(preamble);
    result.append(text);
    result.append(postamble);
  } else {
    result.append(text);
  }

  return result;
}

test_iolayer::test_iolayer(std::shared_ptr<os_access_mock> os)
    : os_{std::move(os)}
    , term_{std::make_shared<test_terminal>(out_, err_)}
    , iol_{std::make_unique<iolayer>(iolayer{
          .os = os_,
          .term = term_,
          .in = in_,
          .out = out_,
          .err = err_,
      })} {}

test_iolayer::~test_iolayer() = default;

iolayer const& test_iolayer::get() const { return *iol_; }

void test_iolayer::set_terminal_fancy(bool fancy) { term_->set_fancy(fancy); }
void test_iolayer::set_terminal_width(size_t width) { term_->set_width(width); }
void test_iolayer::set_in(std::string in) { in_.str(std::move(in)); }

std::string test_iolayer::out() const { return out_.str(); }
std::string test_iolayer::err() const { return err_.str(); }

} // namespace dwarfs::test
