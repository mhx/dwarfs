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

#include <stdexcept>
#include <string_view>
#include <vector>

#include <fmt/format.h>

#include <dwarfs/internal/glob_to_regex.h>

namespace dwarfs::internal {

namespace {

std::string_view constexpr special_chars = R"(.^$|()[]{}+?*\)";

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
