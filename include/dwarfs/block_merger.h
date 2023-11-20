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

namespace dwarfs {

class block_merger_base {
 public:
  virtual ~block_merger_base() = default;

  virtual void release() = 0;
};

template <typename T>
class merged_block_holder {
 public:
  using block_type = T;

  merged_block_holder() = default;

  explicit merged_block_holder(block_type&& blk)
      : block_{std::move(blk)} {}

  merged_block_holder(block_type&& blk,
                      std::shared_ptr<block_merger_base> merger)
      : block_{std::move(blk)}
      , merger_{std::move(merger)} {}

  ~merged_block_holder() {
    if (merger_) {
      merger_->release();
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
