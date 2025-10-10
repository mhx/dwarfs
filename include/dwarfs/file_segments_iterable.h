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

#include <dwarfs/file_range.h>
#include <dwarfs/file_segment.h>

namespace dwarfs {

namespace detail {
class file_view_impl;
}

class file_segments_iterable {
 public:
  file_segments_iterable(std::shared_ptr<detail::file_view_impl const> fv,
                         file_range range, size_t max_segment_bytes,
                         size_t overlap_bytes) noexcept;

  class iterator {
   public:
    using value_type = file_segment;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::input_iterator_tag;
    using reference = file_segment const&;
    using pointer = file_segment const*;

    iterator();
    iterator(std::shared_ptr<detail::file_view_impl const> fv, file_range range,
             size_t maxb, size_t overlapb);
    ~iterator();

    reference operator*() const noexcept { return seg_; }
    pointer operator->() const noexcept { return &seg_; }

    iterator& operator++() {
      advance();
      return *this;
    }

    iterator operator++(int) {
      auto tmp = *this;
      ++*this;
      return tmp;
    }

    friend bool operator==(iterator const& a, std::default_sentinel_t) {
      return !a.fv_ || a.at_end_;
    }

    friend bool operator!=(iterator const& a, std::default_sentinel_t s) {
      return !(a == s);
    }

   private:
    void advance();

    std::shared_ptr<detail::file_view_impl const> fv_;
    file_range range_;
    size_t max_bytes_{};
    size_t overlap_bytes_{};
    file_off_t offset_{};
    file_segment seg_;
    bool at_end_{true};
  };

  static_assert(std::input_iterator<iterator>);

  iterator begin() const noexcept {
    return iterator{fv_, range_, max_bytes_, overlap_bytes_};
  }

  std::default_sentinel_t end() const noexcept { return {}; }

 private:
  std::shared_ptr<detail::file_view_impl const> fv_;
  file_range const range_;
  size_t const max_bytes_{};
  size_t const overlap_bytes_{};
};

} // namespace dwarfs
