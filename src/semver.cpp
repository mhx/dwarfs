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

#include <array>
#include <charconv>
#include <ostream>

#include <dwarfs/semver.h>

namespace dwarfs {

std::optional<semver> semver::parse(std::string_view str) {
  std::array<unsigned, 3> parts{};
  char const* p = str.data();
  char const* end = str.data() + str.size();

  for (std::size_t i = 0; i < parts.size(); ++i) {
    if (i > 0) {
      if (p == end || *p != '.') {
        return std::nullopt;
      }
      ++p;
    }
    auto [next, ec] = std::from_chars(p, end, parts[i]);
    if (ec != std::errc{}) {
      return std::nullopt;
    }
    p = next;
  }

  if (p != end && *p != '-' && *p != '+') {
    return std::nullopt;
  }

  return semver{parts[0], parts[1], parts[2]};
}

std::optional<semver>
semver::parse(std::string_view str, std::string_view prefix) {
  if (str.starts_with(prefix)) {
    str.remove_prefix(prefix.size());
  }

  return parse(str);
}

std::ostream& operator<<(std::ostream& os, semver const& v) {
  return os << v.major << '.' << v.minor << '.' << v.patch;
}

} // namespace dwarfs
