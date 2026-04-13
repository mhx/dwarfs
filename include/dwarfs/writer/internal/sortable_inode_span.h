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
  using value_type = inode_ptr const;
  using index_value_type = uint32_t;

  class iterator
      : public boost::iterator_facade<iterator, value_type,
                                      boost::random_access_traversal_tag> {
   public:
    using difference_type = boost::iterator_facade<
        iterator, value_type,
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

    value_type const& dereference() const { return vv_->values_[*it_]; }

    sortable_inode_span const* vv_{nullptr};
    std::vector<index_value_type>::iterator it_;
  };

  explicit sortable_inode_span(entry_storage& storage,
                               std::span<value_type> values)
      : storage_{&storage}
      , values_{values} {}

  template <typename P>
  void select(P const& predicate) {
    index_.reserve(values_.size());
    for (size_t i = 0; i < values_.size(); ++i) {
      if (predicate(values_[i])) {
        index_.push_back(i);
      }
    }
    index_.shrink_to_fit();
  }

  void all() {
    index_.resize(values_.size());
    std::iota(index_.begin(), index_.end(), 0);
  }

  bool empty() const { return index_.empty(); }
  size_t size() const { return index_.size(); }

  value_type const& operator[](size_t i) const { return values_[index_[i]]; }

  iterator begin() { return iterator(this, index_.begin()); }

  iterator end() { return iterator(this, index_.end()); }

  std::vector<index_value_type>& index() { return index_; }
  std::vector<index_value_type> const& index() const { return index_; }

  // [[deprecated]]
  std::span<value_type> raw() const { return values_; }

  inode_handle handle(size_t i) const { return raw_handle(index_[i]); }

  inode_handle raw_handle(std::size_t i) const {
    return inode_handle{*storage_, i};
  }

  std::size_t raw_size() const { return values_.size(); }

  // TODO: we can later refactor this to return read-only segmented
  //       packed vectors, or at least something span-like / index-based
  //       that can be used to access vector elements without the need
  //       to use handles and jump through multiple interface layers

 private:
  entry_storage* storage_{nullptr};
  std::vector<index_value_type> index_;
  std::span<value_type> const values_;
};

} // namespace dwarfs::writer::internal
