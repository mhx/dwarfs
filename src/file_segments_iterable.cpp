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

#include <cassert>
#include <utility>

#include <dwarfs/detail/file_view_impl.h>
#include <dwarfs/file_segments_iterable.h>

namespace dwarfs {

file_segments_iterable::file_segments_iterable(
    std::shared_ptr<detail::file_view_impl const> fv, file_range range,
    size_t max_segment_bytes, size_t overlap_bytes) noexcept
    : fv_{std::move(fv)}
    , range_{range}
    , max_bytes_{max_segment_bytes == 0 ? fv_->default_segment_size()
                                        : max_segment_bytes}
    , overlap_bytes_{overlap_bytes} {}

file_segments_iterable::iterator::iterator() = default;
file_segments_iterable::iterator::~iterator() = default;

file_segments_iterable::iterator::iterator(
    std::shared_ptr<detail::file_view_impl const> fv, file_range range,
    size_t maxb, size_t overlapb)
    : fv_{std::move(fv)}
    , range_{range}
    , max_bytes_{maxb}
    , overlap_bytes_{overlapb}
    , at_end_{!fv_} {
  assert(max_bytes_ > 0);
  assert(overlap_bytes_ < max_bytes_);
  if (fv_) {
    advance();
  }
}

void file_segments_iterable::iterator::advance() {
  auto const size = range_.size();
  at_end_ = std::cmp_greater_equal(offset_, size);

  if (at_end_) {
    seg_.reset();
    fv_.reset();
  } else {
    auto const len = std::min<size_t>(max_bytes_, size - offset_);
    seg_ = fv_->segment_at(range_.subrange(offset_, len));
    offset_ += len;
    if (std::cmp_less(offset_, size)) {
      offset_ -= overlap_bytes_;
    }
  }
}

bool file_segments_iterable::iterator::equal(iterator const& a,
                                             iterator const& b) noexcept {
  return a.fv_.get() == b.fv_.get() && a.range_ == b.range_ &&
         a.offset_ == b.offset_ && a.max_bytes_ == b.max_bytes_ &&
         a.at_end_ == b.at_end_;
}

} // namespace dwarfs
