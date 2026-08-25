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

#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

#if __has_include(<utf8cpp/utf8.h>)
#include <utf8cpp/utf8.h>
#else
#include <utf8.h>
#endif

#include <dwarfs/tool/render_manpage.h>

namespace dwarfs::tool {

namespace {

size_t utf8_length(std::string_view sv) {
  return static_cast<size_t>(utf8::distance(sv.begin(), sv.end()));
}

size_t utf8_offset(std::string_view sv, size_t n) {
  auto it = sv.begin();
  for (size_t i = 0; i < n && it != sv.end(); ++i) {
    utf8::next(it, sv.end());
  }
  return static_cast<size_t>(std::distance(sv.begin(), it));
}

} // namespace

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

  auto const indent_line = [&out_it](uint32_t indent) {
    fmt::format_to(out_it, "{}", std::string(indent, ' '));
  };

  auto const break_line = [&out_it, &indent_line](uint32_t indent) {
    fmt::format_to(out_it, "\n");
    indent_line(indent);
  };

  static constexpr size_t min_text_width = 8;
  size_t const max_indent =
      effective_width - std::min(effective_width, min_text_width);
  auto const clamp_indent = [max_indent](uint32_t i) {
    return static_cast<uint32_t>(std::min<size_t>(i, max_indent));
  };

  for (auto const& l : doc) {
    auto const indent_first = clamp_indent(l.indent_first);
    auto const indent_next = clamp_indent(l.indent_next);

    indent_line(indent_first);

    if (l.no_wrap) {
      for (auto const& e : l.elements) {
        fmt::format_to(out_it, color ? e.style : fmt::text_style{}, "{}",
                       e.text);
      }
      fmt::format_to(out_it, "\n");
      continue;
    }

    uint32_t indent = indent_first;
    size_t column = indent;

    for (size_t i = 0; i < l.elements.size(); ++i) {
      auto const& e = l.elements[i];
      auto const* next =
          (i + 1 < l.elements.size()) ? &l.elements[i + 1] : nullptr;
      auto t = e.text;
      auto style = color ? e.style : fmt::text_style{};
      auto len = utf8_length(t);

      while (!t.empty() && column + len > effective_width) {
        size_t const avail =
            column < effective_width ? effective_width - column : 0;
        size_t const limit = utf8_offset(t, avail);
        auto wp = t.rfind(' ', limit);
        size_t skip = 1;

        if (wp == std::string_view::npos) {
          if (column > indent) {
            indent = indent_next;
            break_line(indent);
            column = indent;
            continue;
          }

          wp = limit > 0 ? limit : utf8_offset(t, 1);
          skip = 0;
        }

        fmt::format_to(out_it, style, "{}", t.substr(0, wp));
        t = t.substr(wp + skip);
        len = utf8_length(t);

        indent = indent_next;
        break_line(indent);
        column = indent;
      }

      if (column + len > effective_width) {
        throw std::logic_error("line too long");
      }

      if (column + len == effective_width && next != nullptr &&
          next->text.size() == 1 && punct.contains(next->text[0])) {
        indent = indent_next;
        break_line(indent);
        column = indent;
      }

      fmt::format_to(out_it, style, "{}", t);
      column += len;
    }

    fmt::format_to(out_it, "\n");
  }

  return out;
}

} // namespace dwarfs::tool
