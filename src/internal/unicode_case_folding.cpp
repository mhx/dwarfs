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

#if __has_include(<utf8cpp/utf8.h>)
#include <utf8cpp/utf8.h>
#else
#include <utf8.h>
#endif

#include <dwarfs/internal/unicode_case_folding.h>

namespace dwarfs::internal {

char32_t case_fold_char_slow(char32_t in) {
  // NOLINTBEGIN(bugprone-branch-clone)
  switch (in) {
#include "unicode_case_folding_case.inc"
  default:
    return in;
  }
  // NOLINTEND(bugprone-branch-clone)
}

namespace {

template <typename InputIt, typename OutputIt>
void case_fold_impl(InputIt start, InputIt end, OutputIt out) {
  while (start < end) {
    auto cp = utf8::next(start, end);
    auto folded = case_fold_char(cp);
    out = utf8::append(folded, out);
  }
}

template <typename InputIt, typename OutputIt>
void case_fold_unchecked_impl(InputIt start, InputIt end, OutputIt out) {
  while (start < end) {
    auto cp = utf8::unchecked::next(start);
    auto folded = case_fold_char(cp);
    out = utf8::unchecked::append(folded, out);
  }
}

} // namespace

std::string utf8_case_fold(std::string_view in) {
  std::string out;
  out.reserve(in.size());
  case_fold_impl(in.begin(), in.end(), std::back_inserter(out));
  return out;
}

std::string utf8_case_fold_unchecked(std::string_view in) {
  std::string out;
  out.reserve(in.size());
  case_fold_unchecked_impl(in.begin(), in.end(), std::back_inserter(out));
  return out;
}

} // namespace dwarfs::internal
