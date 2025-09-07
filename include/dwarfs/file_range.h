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

#include <cassert>
#include <concepts>
#include <limits>

#include <dwarfs/types.h>

namespace dwarfs {

class file_range {
 public:
  file_range() = default;
  file_range(file_off_t begin, file_off_t end)
      : begin_{begin}
      , end_{end} {
    assert(end_ >= begin_);
  }

  file_off_t begin() const noexcept { return begin_; }

  file_off_t end() const noexcept { return end_; }

  file_size_t size() const noexcept { return end_ - begin_; }

  friend bool
  operator==(file_range const& lhs, file_range const& rhs) noexcept {
    return lhs.begin_ == rhs.begin_ && lhs.end_ == rhs.end_;
  }

 private:
  file_off_t begin_{0};
  file_off_t end_{0};
};

} // namespace dwarfs
