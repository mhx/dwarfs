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
#include <array>
#include <cstdint>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include <fmt/color.h>
#include <fmt/format.h>

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

// Keep these common layouts in sync with LINE_LAYOUTS in
// cmake/render_manpage_v2.py. Other layouts use the 0x1f escape.
struct layout {
  uint8_t indent_first;
  uint8_t indent_next;
  bool no_wrap;
};

constexpr std::array<layout, 10> layouts{{
    {0, 0, false},
    {4, 4, false},
    {8, 8, false},
    {8, 10, false},
    {8, 11, false},
    {8, 16, false},
    {12, 12, true},
    {14, 14, true},
    {16, 16, false},
    {20, 20, true},
}};

bool is_line_command(unsigned char byte) {
  return byte >= 0x10 && byte <= 0x1f;
}

bool is_style_command(unsigned char byte) {
  return byte >= 0x01 && byte <= 0x07;
}

struct line_view {
  layout format;
  std::string_view text;
};

std::optional<line_view> next_line(std::string_view& data) {
  if (data.empty()) {
    return std::nullopt;
  }

  auto const command = static_cast<unsigned char>(data.front());
  layout format;

  if (command == 0x1f) {
    if (data.size() < 4) {
      throw std::invalid_argument("truncated manpage line layout");
    }

    format = {static_cast<uint8_t>(data[1]), static_cast<uint8_t>(data[2]),
              data[3] != 0};

    data.remove_prefix(4);
  } else if (command >= 0x10 && command - 0x10 < layouts.size()) {
    format = layouts[command - 0x10];
    data.remove_prefix(1);
  } else {
    throw std::invalid_argument("invalid manpage line layout");
  }

  size_t count = 0;

  while (count < data.size()) {
    auto const byte = static_cast<unsigned char>(data[count]);

    if (is_line_command(byte)) {
      break;
    }

    if (byte == 0x07) {
      if (count + 1 == data.size()) {
        throw std::invalid_argument("truncated manpage style mask");
      }

      ++count; // The mask may itself look like a line command.
    }

    ++count;
  }

  auto text = data.substr(0, count);

  data.remove_prefix(count);

  return line_view{format, text};
}

struct text_run {
  uint8_t style;
  std::string_view text;
};

std::optional<text_run> next_run(std::string_view& data, uint8_t& style) {
  while (!data.empty()) {
    auto const command = static_cast<unsigned char>(data.front());

    if (!is_style_command(command)) {
      break;
    }

    data.remove_prefix(1);

    if (command == 0x07) {
      if (data.empty()) {
        throw std::invalid_argument("truncated manpage style mask");
      }

      style =
          static_cast<uint8_t>(0x80 | static_cast<unsigned char>(data.front()));

      data.remove_prefix(1);
    } else {
      style = command - 1;
    }
  }

  if (data.empty()) {
    return std::nullopt;
  }

  size_t count = 0;

  while (count < data.size() &&
         !is_style_command(static_cast<unsigned char>(data[count]))) {
    ++count;
  }

  auto text = data.substr(0, count);

  data.remove_prefix(count);

  return text_run{style, text};
}

fmt::text_style text_style(uint8_t code) {
  switch (code) {
  case 0:
    return {};
  case 1:
    return fmt::emphasis::bold;
  case 2:
    return fmt::emphasis::italic;
  case 3:
    return fmt::fg(fmt::terminal_color::bright_green) | fmt::emphasis::bold;
  case 4:
    return fmt::fg(fmt::terminal_color::bright_blue) | fmt::emphasis::bold;
  case 5:
    return fmt::emphasis::faint;
  default:
    break;
  }

  // Styles not in the fast-path table are encoded as a bit mask.
  auto const mask = code & 0x1f;
  fmt::text_style style;

  if (mask & 1) {
    style = style | fmt::emphasis::bold;
  }

  if (mask & 2) {
    style = style | fmt::emphasis::italic;
  }

  if (mask & 4) {
    style = style | fmt::fg(fmt::terminal_color::bright_green) |
            fmt::emphasis::bold;
  }

  if (mask & 8) {
    style =
        style | fmt::fg(fmt::terminal_color::bright_blue) | fmt::emphasis::bold;
  }

  if (mask & 16) {
    style = style | fmt::emphasis::faint;
  }

  return style;
}

} // namespace

std::string
render_manpage(manpage::document doc, size_t const width, bool const color) {
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

  while (auto l = next_line(doc)) {
    auto const indent_first = clamp_indent(l->format.indent_first);
    auto const indent_next = clamp_indent(l->format.indent_next);

    indent_line(indent_first);

    auto text = l->text;
    uint8_t current_style = 0;
    auto e = next_run(text, current_style);

    if (l->format.no_wrap) {
      while (e) {
        fmt::format_to(out_it, color ? text_style(e->style) : fmt::text_style{},
                       "{}", e->text);
        e = next_run(text, current_style);
      }

      fmt::format_to(out_it, "\n");

      continue;
    }

    uint32_t indent = indent_first;
    size_t column = indent;

    while (e) {
      auto const next = next_run(text, current_style);
      auto t = e->text;
      auto style = color ? text_style(e->style) : fmt::text_style{};
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

      if (column + len == effective_width && next && next->text.size() == 1 &&
          punct.contains(next->text[0])) {
        indent = indent_next;
        break_line(indent);
        column = indent;
      }

      fmt::format_to(out_it, style, "{}", t);
      column += len;
      e = next;
    }

    fmt::format_to(out_it, "\n");
  }

  return out;
}

} // namespace dwarfs::tool
