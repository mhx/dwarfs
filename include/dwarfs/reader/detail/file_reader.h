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

#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include <dwarfs/counting_semaphore.h>
#include <dwarfs/reader/block_range.h>
#include <dwarfs/reader/metadata_types.h>

namespace dwarfs::reader {

class filesystem_v2_lite;

namespace detail {

class block_range_iterable {
 public:
  class state;

  class iterator {
   public:
    using value_type = block_range;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::input_iterator_tag;
    using reference = value_type const&;
    using pointer = value_type const*;

    iterator() = default;
    explicit iterator(std::shared_ptr<state> state);

    reference operator*() const noexcept;

    iterator& operator++();

    void operator++(int) { ++*this; }

    friend bool
    operator==(iterator const& it, std::default_sentinel_t) noexcept {
      return !it.state_;
    }

   private:
    std::optional<block_range> mutable cur_;
    std::shared_ptr<state> state_;
  };

  static_assert(std::input_iterator<iterator>);

  explicit block_range_iterable(std::shared_ptr<state> state)
      : state_{std::move(state)} {}

  iterator begin() const { return iterator(state_); }
  std::default_sentinel_t end() const { return {}; }

 private:
  std::shared_ptr<state> state_;
};

class file_reader {
 public:
  file_reader(filesystem_v2_lite const& fs, inode_view iv);

  file_size_t size() const noexcept { return size_; }

  std::vector<dwarfs::detail::file_extent_info> extents() const;

  block_range_iterable
  read_sequential(std::span<file_range const> ranges, counting_semaphore& sem,
                  file_size_t max_bytes);
  block_range_iterable
  read_sequential(counting_semaphore& sem, file_size_t max_bytes);

 private:
  filesystem_v2_lite const& fs_;
  inode_view iv_;
  file_size_t size_{0};
};

} // namespace detail

} // namespace dwarfs::reader
