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

#include <iterator>
#include <stdexcept>

#include <dwarfs/tool/render_manpage.h>

namespace dwarfs::tool {

std::string render_manpage(manpage::document const doc, size_t const width,
                           bool const color) {
  static constexpr size_t min_width = 20;

  if (width < min_width) {
    throw std::invalid_argument("width too small");
  }

  static constexpr std::string_view punct = ".,:;!?";
  static constexpr size_t right_margin = 4;
  size_t const effective_width = width - right_margin;
  std::string out;
  auto out_it = std::back_inserter(out);

  for (auto const& l : doc) {
    uint32_t indent = l.indent_first;
    uint32_t column = indent;

    fmt::format_to(out_it, "{}", std::string(indent, ' '));

    for (size_t i = 0; i < l.elements.size(); ++i) {
      auto e = l.elements[i];
      auto* next = (i + 1 < l.elements.size()) ? &l.elements[i + 1] : nullptr;
      auto t = e.text;
      auto style = color ? e.style : fmt::text_style{};

      while (!t.empty() && column + t.size() > effective_width) {
        auto wp = t.rfind(' ', effective_width - column);
        auto const space_offset = wp == std::string_view::npos ? 0 : 1;

        if (wp == std::string_view::npos && column == indent) {
          if (column < effective_width) {
            wp = effective_width - column;
          } else {
            wp = 1;
          }
        }

        if (wp != std::string_view::npos) {
          fmt::format_to(out_it, style, "{}", t.substr(0, wp));
          t = t.substr(wp + space_offset);
        }

        indent = l.indent_next;
        fmt::format_to(out_it, "\n{}", std::string(indent, ' '));
        column = indent;
      }

      if (column + t.size() > effective_width) {
        throw std::logic_error("line too long");
      }

      if (column + t.size() == effective_width && next &&
          next->text.size() == 1 &&
          punct.find(next->text[0]) != std::string_view::npos) {
        indent = l.indent_next;
        fmt::format_to(out_it, "\n{}", std::string(indent, ' '));
        column = indent;
      }

      fmt::format_to(out_it, style, "{}", t);
      column += t.size();
    }
    fmt::format_to(out_it, "\n");
  }

  return out;
}

} // namespace dwarfs::tool
