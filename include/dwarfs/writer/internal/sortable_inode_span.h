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
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <cassert>
#include <numeric>
#include <span>
#include <vector>

#include <boost/iterator/iterator_facade.hpp>

#include <dwarfs/writer/internal/entry_storage.h>
#include <dwarfs/writer/internal/inode.h>
#include <dwarfs/writer/internal/inode_handle.h>

namespace dwarfs::writer::internal {

class sortable_inode_span {
 public:
  using index_value_type = uint32_t;

  class iterator
      : public boost::iterator_facade<iterator, inode_handle,
                                      boost::random_access_traversal_tag,
                                      inode_handle> {
   public:
    using difference_type = boost::iterator_facade<
        iterator, inode_handle,
        boost::random_access_traversal_tag>::difference_type;

    iterator() = default;
    iterator(iterator const& other) = default;

   private:
    friend class boost::iterator_core_access;
    friend class sortable_inode_span;

    iterator(sortable_inode_span const* vv,
             std::vector<index_value_type>::iterator it)
        : vv_(vv)
        , it_(it) {}

    bool equal(iterator const& other) const {
      return vv_ == other.vv_ && it_ == other.it_;
    }

    void increment() { ++it_; }

    void decrement() { --it_; }

    void advance(difference_type n) { it_ += n; }

    difference_type distance_to(iterator const& other) const {
      return other.it_ - it_;
    }

    inode_handle dereference() const { return vv_->mutable_raw_handle(*it_); }

    sortable_inode_span const* vv_{nullptr};
    std::vector<index_value_type>::iterator it_;
  };

  explicit sortable_inode_span(entry_storage& storage)
      : storage_{&storage}
      , raw_size_{storage.inode_count()} {}

  template <typename P>
  void select(P const& predicate) {
    for (size_t i = 0; i < raw_size_; ++i) {
      if (predicate(raw_handle(i))) {
        index_.push_back(i);
      }
    }
  }

  void all() {
    index_.resize(raw_size_);
    // NOLINTNEXTLINE(modernize-use-ranges)
    std::iota(index_.begin(), index_.end(), 0);
  }

  bool empty() const { return index_.empty(); }
  size_t size() const { return index_.size(); }

  iterator begin() { return {this, index_.begin()}; }

  iterator end() { return {this, index_.end()}; }

  std::vector<index_value_type>& index() { return index_; }
  std::vector<index_value_type> const& index() const { return index_; }

  const_inode_handle handle(size_t i) const { return raw_handle(index_[i]); }

  // TODO: size_t -> inode_id?
  const_inode_handle raw_handle(std::size_t i) const {
    return {*storage_, inode_id{i}};
  }

  // TODO: size_t -> inode_id?
  inode_handle mutable_raw_handle(std::size_t i) const {
    return {*storage_, inode_id{i}};
  }

  std::size_t raw_size() const { return raw_size_; }

  // TODO: we can later refactor this to return read-only segmented
  //       packed vectors, or at least something span-like / index-based
  //       that can be used to access vector elements without the need
  //       to use handles and jump through multiple interface layers

 private:
  entry_storage* storage_{nullptr};
  std::vector<index_value_type> index_;
  std::size_t const raw_size_{0};
};

} // namespace dwarfs::writer::internal
