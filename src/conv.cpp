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
#include <cctype>

#include <dwarfs/conv.h>

namespace dwarfs::detail {

namespace {

bool eq_nocase(std::string_view const a, std::string_view const b) {
  return std::ranges::equal(a, b, [](char const a, char const b) {
    return std::tolower(a) == std::tolower(b);
  });
}

} // namespace

std::optional<bool> str_to_bool(std::string_view s) {
  auto const len = s.size();

  if (len > 0) {
    switch (s.front()) {
    case '0':
    case '1':
      if (len == 1) {
        return s.front() == '1';
      }
      break;

    case 'f':
    case 'F':
      if (len == 1 || eq_nocase(s, "false")) {
        return false;
      }
      break;

    case 'n':
    case 'N':
      if (len == 1 || eq_nocase(s, "no")) {
        return false;
      }
      break;

    case 't':
    case 'T':
      if (len == 1 || eq_nocase(s, "true")) {
        return true;
      }
      break;

    case 'y':
    case 'Y':
      if (len == 1 || eq_nocase(s, "yes")) {
        return true;
      }
      break;

    default:
      break;
    }
  }

  return std::nullopt;
}

} // namespace dwarfs::detail
