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
#include <cmath>
#include <numeric>
#include <vector>

#include <dwarfs/terminal.h>
#include <dwarfs/termutil.h>

namespace dwarfs {

namespace {

struct bar_color {
  termcolor fg;
  termcolor bg;
};

constexpr std::array bar_colors = {
    bar_color{termcolor::WHITE, termcolor::DIM_BLUE},
    bar_color{termcolor::BLACK, termcolor::YELLOW},
    bar_color{termcolor::WHITE, termcolor::RED},
    bar_color{termcolor::BLACK, termcolor::CYAN},
    bar_color{termcolor::WHITE, termcolor::MAGENTA},
    bar_color{termcolor::BLACK, termcolor::GREEN},
};

} // namespace

std::string render_bar_chart(terminal const& term,
                             std::span<bar_chart_section const> bars) {
  auto const width = term.width();

  if (bars.empty() || width == 0) {
    return {};
  }

  // ----- 1. how many character cells for each bar? -----
  double const total_fraction = std::accumulate(
      bars.begin(), bars.end(), 0.0,
      [](double s, bar_chart_section const& b) { return s + b.fraction; });

  struct chunk {
    size_t len = 0;       // integer width actually used
    double remainder = 0; // fractional remainder (for redistribution)
  };
  std::vector<chunk> chunks(bars.size());

  size_t used = 0;
  for (size_t i = 0; i < bars.size(); ++i) {
    double const raw =
        (total_fraction == 0.0
             ? 0.0
             : bars[i].fraction / total_fraction * static_cast<double>(width));
    chunks[i].len = static_cast<size_t>(std::floor(raw));
    chunks[i].remainder = raw - static_cast<double>(chunks[i].len);
    used += chunks[i].len;
  }

  // distribute the remaining columns to the bars with the largest remainders
  size_t missing = width - used;
  while (missing > 0) {
    auto const it =
        std::max_element(chunks.begin(), chunks.end(), [](auto& a, auto& b) {
          return a.remainder < b.remainder;
        });
    ++it->len;
    it->remainder = 0.0; // so it doesn’t get another extra column
    --missing;
  }

  // ----- 2. build the coloured string bar by bar -----
  std::string result;
  result.reserve(width * 16); // rough guess, avoids many reallocations

  for (size_t i = 0; i < bars.size(); ++i) {
    auto const w = chunks[i].len;
    if (w == 0) {
      continue;
    } // fractions can be tiny – simply skip

    auto const& lbl = bars[i].label;
    auto const& color = bar_colors[i % bar_colors.size()];

    std::string segment(w, ' '); // filled with spaces → solid background

    if (lbl.size() >= w) { // truncate
      segment.assign(lbl.substr(0, w));
    } else { // centre
      std::size_t const left = (w - lbl.size()) / 2;
      segment.replace(left, lbl.size(), lbl);
    }

    result += term.bgcolor(color.bg);
    result += term.color(color.fg);
    result += segment;
  }

  result += term.bgcolor(termcolor::NORMAL);
  result += term.color(termcolor::NORMAL);

  return result;
}

} // namespace dwarfs
