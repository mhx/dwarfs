/* vim:set ts=2 sw=2 sts=2 et: */
/**
 * \author     Marcus Holland-Moritz (github@mhxnet.de)
 * \copyright  Copyright (c) Marcus Holland-Moritz
 *
 * This file is part of dwarfs.
 *
 * dwarfs is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * dwarfs is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with dwarfs.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <memory>
#include <stdexcept>

#include <fmt/format.h>

namespace dwarfs {

class block_merger_base {
 public:
  virtual ~block_merger_base() = default;

  virtual void release(size_t amount) = 0;
};

template <typename T>
class merged_block_holder {
 public:
  using block_type = T;

  merged_block_holder() = default;

  explicit merged_block_holder(block_type&& blk)
      : block_{std::move(blk)} {}

  merged_block_holder(block_type&& blk, size_t size,
                      std::shared_ptr<block_merger_base> merger)
      : block_{std::move(blk)}
      , size_{size}
      , merger_{std::move(merger)} {}

  ~merged_block_holder() { release(); }

  void release() {
    if (merger_) {
      merger_->release(size_);
    }
  }

  void release_partial(size_t amount) {
    if (amount > size_) {
      throw std::runtime_error(fmt::format(
          "merged_block_holder::release_partial: amount {} > size {}", amount,
          size_));
    }

    size_ -= amount;

    if (merger_) {
      merger_->release(amount);
    }
  }

  merged_block_holder(merged_block_holder&&) = default;
  merged_block_holder& operator=(merged_block_holder&&) = default;

  merged_block_holder(merged_block_holder const&) = delete;
  merged_block_holder& operator=(merged_block_holder const&) = delete;

  block_type& value() & { return block_; }
  block_type const& value() const& { return block_; }

  block_type&& value() && { return std::move(block_); }
  block_type const&& value() const&& { return std::move(block_); }

  block_type* operator->() { return &block_; }
  block_type const* operator->() const { return &block_; }

  block_type& operator*() & { return block_; }
  block_type const& operator*() const& { return block_; }

  block_type&& operator*() && { return std::move(block_); }
  block_type const&& operator*() const&& { return std::move(block_); }

 private:
  block_type block_;
  size_t size_{0};
  std::shared_ptr<block_merger_base> merger_;
};

template <typename SourceT, typename BlockT>
class block_merger {
 public:
  using source_type = SourceT;
  using block_type = BlockT;

  virtual ~block_merger() = default;

  virtual void add(source_type src, block_type blk) = 0;
  virtual void finish(source_type src) = 0;
};

} // namespace dwarfs
