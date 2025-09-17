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
#include <dwarfs/file_extents_iterable.h>

namespace dwarfs {

file_extents_iterable::file_extents_iterable(
    std::shared_ptr<detail::file_view_impl const> fv,
    std::span<detail::file_extent_info const> extents, file_range range)
    : fv_{std::move(fv)}
    , extents_{extents}
    , range_{range} {}

file_extents_iterable::iterator::iterator(
    std::shared_ptr<detail::file_view_impl const> fv,
    std::span<detail::file_extent_info const> extents, file_range range)
    : fv_{std::move(fv)}
    , end_offset_{range.end()}
    , extents_{extents}
    , it_{extents_.begin()} {
  if (!range.empty()) {
    while (it_ != extents_.end() && it_->range.end() <= range.offset()) {
      ++it_;
    }
    if (it_ != extents_.end()) {
      auto ext = *it_;
      ext.range =
          file_range{range.offset(),
                     std::min(ext.range.end(), end_offset_) - range.offset()};
      cur_ = file_extent{fv_, ext};
    }
  }
}

auto file_extents_iterable::iterator::operator++() -> iterator& {
  ++it_;
  if (it_ != extents_.end() && it_->range.offset() < end_offset_) {
    auto ext = *it_;
    if (ext.range.end() > end_offset_) {
      ext.range =
          file_range{ext.range.offset(), end_offset_ - ext.range.offset()};
    }
    assert(fv_);
    cur_ = file_extent{fv_, ext};
  } else {
    it_ = extents_.end();
    fv_.reset();
    cur_ = file_extent{};
  }
  return *this;
}

std::string file_extents_iterable::as_string() const {
  std::string res;

  res.reserve(range_.size());

  for (auto const& ext : *this) {
    for (auto const& seg : ext.segments()) {
      auto const data = seg.span<char>();
      res.append(data.data(), data.size());
    }
  }

  return res;
}

} // namespace dwarfs
