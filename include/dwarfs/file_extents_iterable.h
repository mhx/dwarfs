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
#include <string>

#include <dwarfs/file_extent.h>

namespace dwarfs {

namespace detail {
class file_view_impl;
}

class file_extents_iterable {
 public:
  file_extents_iterable(std::shared_ptr<detail::file_view_impl const> fv,
                        std::span<detail::file_extent_info const> extents,
                        file_range range);

  class iterator {
   public:
    using value_type = file_extent;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::input_iterator_tag;
    using reference = file_extent const&;
    using pointer = file_extent const*;

    iterator() = default;
    iterator(std::shared_ptr<detail::file_view_impl const> fv,
             std::span<detail::file_extent_info const> extents,
             file_range range);

    reference operator*() const noexcept { return cur_; }
    pointer operator->() const noexcept { return &cur_; }

    iterator& operator++();

    iterator operator++(int) {
      auto tmp = *this;
      ++*this;
      return tmp;
    }

    friend bool operator==(iterator const& a, iterator const& b) noexcept {
      return a.fv_ == b.fv_ && a.it_ == b.it_;
    }

    friend bool operator!=(iterator const& a, iterator const& b) noexcept {
      return !(a == b);
    }

    friend bool operator==(iterator const& a, std::default_sentinel_t) {
      return a.it_ == a.extents_.end();
    }

    friend bool operator==(std::default_sentinel_t s, iterator const& a) {
      return a == s;
    }

    friend bool operator!=(iterator const& a, std::default_sentinel_t s) {
      return !(a == s);
    }

    friend bool operator!=(std::default_sentinel_t s, iterator const& a) {
      return !(a == s);
    }

   private:
    std::shared_ptr<detail::file_view_impl const> fv_;
    file_extent cur_;
    file_off_t end_offset_{0};
    std::span<detail::file_extent_info const> extents_;
    std::span<detail::file_extent_info const>::iterator it_;
  };

  static_assert(std::input_iterator<iterator>);

  iterator begin() const noexcept { return iterator{fv_, extents_, range_}; }
  std::default_sentinel_t end() const noexcept { return {}; }

  std::string as_string() const;

 private:
  std::shared_ptr<detail::file_view_impl const> fv_;
  std::span<detail::file_extent_info const> extents_;
  file_range range_;
};

} // namespace dwarfs
