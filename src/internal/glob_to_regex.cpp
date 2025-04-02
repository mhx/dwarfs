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

#include <stdexcept>
#include <string_view>
#include <vector>

#include <fmt/format.h>

#include <dwarfs/internal/glob_to_regex.h>

namespace dwarfs::internal {

namespace {

constexpr std::string_view special_chars = R"(.^$|()[]{}+?*\)";

std::string escape_special(char c) {
  std::string esc;
  if (special_chars.find(c) != std::string_view::npos) {
    esc = '\\';
  }
  return esc + c;
}

std::pair<std::string, size_t>
handle_char_set(std::string_view sv, size_t pos) {
  size_t const len = sv.size();
  std::string char_class = "[";
  auto subpat = sv.substr(pos);
  size_t firstchar = pos + 1;

  if (subpat.starts_with("[!]")) {
    char_class += R"(^\])";
    pos += 2;
    ++firstchar;
  } else if (subpat.starts_with("[!")) {
    char_class += R"(^)";
    pos += 1;
    ++firstchar;
  } else if (subpat.starts_with("[]")) {
    char_class += R"(\])";
    pos += 1;
  } else if (subpat.starts_with("[^")) {
    char_class += R"(\^)";
    pos += 1;
  }

  while (++pos < len) {
    char c = sv[pos];
    char_class += c;

    switch (c) {
    case ']':
      return {char_class, pos + 1};

    case '\\':
      char_class += '\\';
      break;

    case '-':
      if (pos > firstchar && pos + 1 < len && sv[pos + 1] != ']') {
        auto from = sv[pos - 1];
        auto to = sv[pos + 1];

        if (from <= '/' && '/' <= to) {
          char_class += ".0-";
        } else if (from > to) {
          throw std::runtime_error(fmt::format("invalid range '{}-{}' in "
                                               "character class in pattern: {}",
                                               from, to, sv));
        }
        firstchar = pos + 2;
      }
      break;

    case '/':
      throw std::runtime_error(
          "invalid character '/' in character class in pattern: " +
          std::string(sv));

    default:
      break;
    }
  }

  throw std::runtime_error("unmatched '[' in pattern: " + std::string(sv));
}

} // namespace

std::string glob_to_regex_string(std::string_view pattern) {
  std::string regex;
  size_t const len = pattern.size();
  size_t pos = 0;
  size_t brace_depth = 0;
  while (pos < len) {
    char c = pattern[pos];
    switch (c) {
    case '\\':
      if (++pos >= len) {
        throw std::runtime_error("trailing backslash in pattern: " +
                                 std::string(pattern));
      }
      regex += escape_special(pattern[pos]);
      ++pos;
      break;

    case '*':
      if (pos + 1 < len && pattern[pos + 1] == '*') {
        if (pos + 2 < len && pattern[pos + 2] == '/' &&
            (pos == 0 || pattern[pos - 1] == '/')) {
          pos += 3;
        } else {
          pos += 2;
        }
        regex += ".*";
      } else {
        bool onlystar = (pos == 0 || pattern[pos - 1] == '/') &&
                        (pos + 1 == len || pattern[pos + 1] == '/');
        ++pos;
        regex += "[^/]";
        regex += onlystar ? '+' : '*';
      }
      break;

    case '?':
      regex += "[^/]";
      ++pos;
      break;

    case '[': {
      auto [char_class, end] = handle_char_set(pattern, pos);
      regex += char_class;
      pos = end;
    } break;

    case '{':
      ++brace_depth;
      regex += "(?:";
      ++pos;
      break;

    case ',':
      regex += brace_depth > 0 ? '|' : c;
      ++pos;
      break;

    case '}':
      if (brace_depth == 0) {
        throw std::runtime_error("unmatched '}' in pattern: " +
                                 std::string(pattern));
      }
      --brace_depth;
      regex += ")";
      ++pos;
      break;

    case ']':
      throw std::runtime_error("unmatched ']' in pattern: " +
                               std::string(pattern));

    default:
      regex += escape_special(c);
      ++pos;
      break;
    }
  }

  if (brace_depth > 0) {
    throw std::runtime_error("unmatched '{' in pattern: " +
                             std::string(pattern));
  }

  return regex;
}

} // namespace dwarfs::internal
