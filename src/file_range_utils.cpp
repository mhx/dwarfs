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

#include <dwarfs/file_range_utils.h>

namespace dwarfs {

std::vector<file_range> intersect_ranges(std::span<file_range const> const a,
                                         std::span<file_range const> const b) {
  std::vector<file_range> out;

  auto ia = a.begin();
  auto ib = b.begin();

  while (ia != a.end() && ib != b.end()) {
    auto const a_beg = ia->begin();
    auto const a_end = ia->end();
    auto const b_beg = ib->begin();
    auto const b_end = ib->end();

    // compute overlap (if any)
    auto const lo = std::max(a_beg, b_beg);
    auto const hi = std::min(a_end, b_end);

    if (lo < hi) {
      out.emplace_back(lo, hi - lo);
    }

    // advance the range that finishes first
    if (a_end <= b_end) {
      ++ia;
    } else {
      ++ib;
    }
  }

  return out;
}

std::vector<file_range>
complement_ranges(std::span<file_range const> const ranges,
                  file_size_t const size) {
  std::vector<file_range> out;

  file_off_t pos = 0;

  for (auto const& h : ranges) {
    if (pos < h.offset()) {
      out.emplace_back(pos, h.offset() - pos);
    }
    pos = h.end();
  }

  if (pos < size) {
    out.emplace_back(pos, size - pos);
  }

  return out;
}

} // namespace dwarfs
