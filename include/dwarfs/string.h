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

#include <iterator>
#include <type_traits>

#include <dwarfs/conv.h>

namespace dwarfs {

template <typename Container>
void split_to(std::string_view str, char delimiter, Container& out) {
  if (str.empty()) {
    return;
  }
  auto add_part =
      [it = std::inserter(out, out.end())](std::string_view part) mutable {
        if constexpr (std::is_constructible_v<typename Container::value_type,
                                              std::string_view>) {
          *it++ = typename Container::value_type(part);
        } else {
          *it++ = to<typename Container::value_type>(part);
        }
      };
  std::size_t start{0};
  std::size_t end = str.find(delimiter);
  while (end != std::string_view::npos) {
    add_part(str.substr(start, end - start));
    start = end + 1;
    end = str.find(delimiter, start);
  }
  add_part(str.substr(start));
}

template <typename Container>
Container split_to(std::string_view str, char delimiter) {
  Container out;
  split_to(str, delimiter, out);
  return out;
}

} // namespace dwarfs
