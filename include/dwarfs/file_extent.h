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
#include <memory>

#include <dwarfs/detail/file_extent_info.h>
#include <dwarfs/detail/file_view_impl.h>
#include <dwarfs/file_segments_iterable.h>

namespace dwarfs {

class file_extent {
 public:
  file_extent() = default;
  file_extent(std::shared_ptr<detail::file_view_impl const> fv,
              detail::file_extent_info const& extent)
      : fv_{std::move(fv)}
      , extent_{&extent} {}

  explicit operator bool() const noexcept { return static_cast<bool>(fv_); }
  bool valid() const noexcept { return static_cast<bool>(fv_); }
  void reset() noexcept { fv_.reset(); }

  file_off_t offset() const noexcept { return extent_->offset; }
  file_size_t size() const noexcept { return extent_->size; }
  extent_kind kind() const noexcept { return extent_->kind; }

  file_range range() const noexcept {
    return {this->offset(), this->offset() + this->size()};
  }

  file_segments_iterable
  segments(size_t max_segment_size = 0, size_t overlap_size = 0) const {
    return file_segments_iterable{fv_, this->range(), max_segment_size,
                                  overlap_size};
  }

 private:
  std::shared_ptr<detail::file_view_impl const> fv_;
  detail::file_extent_info const* extent_{nullptr};
};

} // namespace dwarfs
