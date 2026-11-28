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

// Work around missing <string> include in Boost.Sort.
#include <string>

#include <boost/sort/insert_sort/insert_sort.hpp>

namespace dwarfs {

template <typename Iterator,
          typename Compare = ::boost::sort::compare_iter<Iterator>>
// NOLINTNEXTLINE(performance-unnecessary-value-param)
void insert_sort(Iterator first, Iterator last, Compare comp = Compare()) {
  ::boost::sort::insert_sort(first, last, comp);
}

template <typename Container, typename Compare = ::boost::sort::compare_iter<
                                  typename Container::iterator>>
void insert_sort(Container& c, Compare comp = Compare()) {
  insert_sort(c.begin(), c.end(), comp);
}

} // namespace dwarfs
