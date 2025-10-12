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

#include <concepts>
#include <ranges>
#include <system_error>
#include <vector>

#include <dwarfs/file_range.h>
#include <dwarfs/reader/seek_whence.h>

namespace dwarfs::reader::internal {

template <typename T>
concept chunk_like = requires(T const& chk) {
  { chk.is_hole() } -> std::same_as<bool>;
  { chk.size() } -> std::convertible_to<file_size_t>;
};

class sparse_file_seeker {
 public:
  sparse_file_seeker() = default;

  /**
   * Seek within a sparse file represented by a range of chunks.
   *
   * This is a one-off operation and does not construct a reusable seeker.
   * Since it has to do a linear scan of the chunks on every call, it is
   * easy to end up with O(n²) performance when seeking multiple times.
   *
   * Only use this for one-off seeks, or if the number of chunks is small.
   */
  template <std::ranges::input_range ChunkRange>
    requires chunk_like<std::ranges::range_value_t<ChunkRange>>
  static file_off_t seek(ChunkRange const& chunks, file_off_t const offset,
                         seek_whence const whence, std::error_code& ec) {
    if (offset < 0) {
      ec = std::make_error_code(std::errc::no_such_device_or_address);
      return -1;
    }

    file_off_t pos{0};
    auto chunks_remaining = std::ranges::size(chunks);

    for (auto const& chk : chunks) {
      --chunks_remaining;
      auto const size = chk.size();
      auto const end = pos + size;

      if (chk.is_hole() && offset < end) {
        return get_offset(file_range(pos, size), offset, whence,
                          chunks_remaining == 0, ec);
      }

      pos = end;
    }

    if (offset >= pos) {
      ec = std::make_error_code(std::errc::no_such_device_or_address);
      return -1;
    }

    return get_offset(nullptr, offset, whence, pos, ec);
  }

  /**
   * Construct a sparse file seeker from a range of chunks.
   *
   * This can be reused to seek within the same sparse file multiple times,
   * and calling `seek()` on the instance is significantly faster than calling
   * the static `seek()` method, in particular for files with lots of chunks.
   */
  template <std::ranges::input_range ChunkRange>
    requires chunk_like<std::ranges::range_value_t<ChunkRange>>
  sparse_file_seeker(ChunkRange const& chunks) {
    file_off_t pos{0};

    for (auto const& chk : chunks) {
      auto const size = chk.size();

      if (chk.is_hole()) {
        holes_.emplace_back(pos, size);
      }

      pos += size;
    }

    size_ = pos;
  }

  file_off_t seek(file_off_t const offset, seek_whence const whence,
                  std::error_code& ec) const {
    //                   0-------            1-------
    //        |         |  hole  |          |  hole  |         |
    //        <-----------------><------------------>
    //        offset in this range will find an
    //        iterator to the hole in this range

    if (offset < 0 || offset >= size_) {
      ec = std::make_error_code(std::errc::no_such_device_or_address);
      return -1;
    }

    // `lower_bound` will find the first hole for which hole.end > offset,
    // i.e. either the hole containing `offset` if `offset` is in a hole,
    // or the next hole after `offset` if `offset` is in data. If `offset`
    // is after the last hole, `it` will be `holes_.end()`.

    // NOLINTNEXTLINE(modernize-use-ranges)
    auto const it = std::lower_bound(
        holes_.begin(), holes_.end(), offset,
        [](auto const& hr, auto off) { return hr.end() <= off; });

    return get_offset(it == holes_.end() ? nullptr : &*it, offset, whence,
                      size_, ec);
  }

 private:
  static file_off_t
  get_offset(file_range const* hole, file_off_t const offset,
             seek_whence const whence, file_off_t size, std::error_code& ec) {
    if (!hole) {
      // offset is in data extent after the last hole
      return whence == seek_whence::hole ? size : offset;
    }

    return get_offset(*hole, offset, whence, hole->end() == size, ec);
  }

  static file_off_t get_offset(file_range const& hole, file_off_t const offset,
                               seek_whence const whence,
                               bool const is_last_hole, std::error_code& ec) {
    // offset is either in the hole pointed to by `hole`,
    // or in data before that hole

    if (whence == seek_whence::hole) {
      return std::max(hole.begin(), offset);
    }

    if (offset < hole.begin()) {
      // offset is in data before the hole
      return offset;
    }

    if (is_last_hole) {
      // we are in the last hole, there is no more data
      ec = std::make_error_code(std::errc::no_such_device_or_address);
      return -1;
    }

    return hole.end();
  }

  std::vector<file_range> holes_;
  file_size_t size_{0};
};

} // namespace dwarfs::reader::internal
