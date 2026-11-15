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

#include <array>
#include <cassert>
#include <limits>

#include <dwarfs/binary_literals.h>
#include <dwarfs/container/small_fifo.h>
#include <dwarfs/detail/file_view_impl.h>
#include <dwarfs/error.h>
#include <dwarfs/reader/detail/file_reader.h>
#include <dwarfs/reader/filesystem_v2.h>

namespace dwarfs::reader::detail {

using namespace binary_literals;

class block_range_iterable::state {
 public:
  static file_size_t total_size(std::span<file_range const> ranges) {
    file_size_t size{0};

    for (auto const& r : ranges) {
      if (r.size() < 0 ||
          r.size() > std::numeric_limits<file_size_t>::max() - size) {
        DWARFS_THROW(runtime_error, "invalid sequential read range size");
      }

      size += r.size();
    }

    return size;
  }

  state(filesystem_v2_lite const& fs, uint32_t inode,
        std::span<file_range const> ranges, counting_semaphore& sem,
        file_size_t max_bytes)
      : fs_{fs}
      , inode_{inode}
      , remaining_{total_size(ranges)}
      , lease_{sem, std::min(remaining_, max_bytes)} {
    for (auto const& range : ranges) {
      if (!range.empty()) {
        ranges_.push_back(range);
      }
    }
  }

  bool more() {
    pending_bytes_ -= current_bytes_;
    current_bytes_ = 0;

    if (ranges_.empty()) {
      assert(remaining_ == 0);

      if (pending_.empty()) {
        if (pending_bytes_ != 0) {
          DWARFS_THROW(runtime_error, "short sequential read");
        }
        lease_.release();
        return false;
      }

      lease_.shrink(pending_bytes_);
    } else if (pending_bytes_ < lease_.size()) {
      auto& range = ranges_.front();

      auto const to_read =
          std::min(range.size(), lease_.size() - pending_bytes_);
      assert(to_read > 0);

      auto r = fs_.readv(inode_, to_read, range.offset());

      if (r.empty()) {
        DWARFS_THROW(runtime_error, "empty sequential read");
      }

      if (to_read < range.size()) {
        range.advance(to_read);
      } else {
        ranges_.pop_front();
      }

      pending_bytes_ += to_read;
      remaining_ -= to_read;
      std::ranges::move(r, std::back_inserter(pending_));
    }

    if (pending_.empty()) {
      DWARFS_THROW(runtime_error, "short sequential read");
    }

    return true;
  }

  block_range next() {
    assert(!pending_.empty());
    auto fut = std::move(pending_.front());
    pending_.pop_front();
    auto r = fut.get();
    if (r.size() == 0 || std::cmp_greater(r.size(), pending_bytes_)) {
      DWARFS_THROW(runtime_error, "invalid sequential read size");
    }
    current_bytes_ = r.size();
    return r;
  }

 private:
  template <typename T>
  using fifo_t = container::small_fifo<T, 8>;

  filesystem_v2_lite const& fs_;
  uint32_t inode_;
  fifo_t<file_range> ranges_;
  fifo_t<std::future<block_range>> pending_;
  file_size_t current_bytes_{0};
  file_size_t pending_bytes_{0};
  file_size_t remaining_{0};
  scoped_lease lease_;
};

block_range_iterable::iterator::iterator(std::shared_ptr<state> state)
    : state_{std::move(state)} {}

auto block_range_iterable::iterator::operator*() const -> reference {
  if (!cur_) {
    cur_ = state_->next();
  }
  return cur_.value();
}

block_range_iterable::iterator& block_range_iterable::iterator::operator++() {
  if (cur_) {
    cur_.reset();
  } else {
    // advance to the next range if we didn't dereference the iterator before
    state_->next();
  }

  if (!state_->more()) {
    state_.reset();
  }

  return *this;
}

file_reader::file_reader(filesystem_v2_lite const& fs, inode_view iv)
    : fs_{fs}
    , iv_{std::move(iv)} {
  auto stat = fs_.getattr(iv_);
  DWARFS_CHECK(stat.is_regular_file(), "not a regular file");
  size_ = stat.size();
}

block_range_iterable
file_reader::read_sequential(counting_semaphore& sem, file_size_t max_bytes) {
  std::array<file_range, 1> ranges{file_range{0, size_}};
  return read_sequential(ranges, sem, max_bytes);
}

block_range_iterable
file_reader::read_sequential(std::span<file_range const> ranges,
                             counting_semaphore& sem, file_size_t max_bytes) {
  if (max_bytes <= 0) {
    DWARFS_THROW(runtime_error, "sequential read byte limit must be positive");
  }

  for (auto const& range : ranges) {
    if (range.offset() < 0 || range.size() < 0 || range.offset() > size_ ||
        range.size() > size_ - range.offset()) {
      DWARFS_THROW(runtime_error, "sequential read range outside file");
    }
  }

  auto state = std::make_shared<block_range_iterable::state>(
      fs_, iv_.inode_num(), ranges, sem, max_bytes);

  if (!state->more()) {
    state.reset();
  }

  return block_range_iterable{std::move(state)};
}

std::vector<dwarfs::detail::file_extent_info> file_reader::extents() const {
  std::vector<dwarfs::detail::file_extent_info> extents;
  auto const inode = iv_.inode_num();

  auto whence = seek_whence::data;
  file_off_t offset{0};

  for (;;) {
    std::error_code ec;
    auto rv = fs_.seek(inode, offset, whence, ec);

    if (ec) {
      if (ec == std::errc::no_such_device_or_address) {
        break;
      }
      throw std::system_error(ec);
    }

    extent_kind kind;

    switch (whence) {
    case seek_whence::data:
      kind = extent_kind::hole;
      whence = seek_whence::hole;
      break;
    case seek_whence::hole:
      kind = extent_kind::data;
      whence = seek_whence::data;
      break;
    default:
      DWARFS_PANIC("invalid whence");
    }

    if (rv > offset) {
      extents.emplace_back(kind, file_range{offset, rv - offset});
      offset = rv;
    }
  }

  if (size_ > offset) {
    extents.emplace_back(extent_kind::hole, file_range{offset, size_ - offset});
  }

  return extents;
}

} // namespace dwarfs::reader::detail
