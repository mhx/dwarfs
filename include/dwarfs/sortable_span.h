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

#include <cassert>
#include <numeric>
#include <span>
#include <vector>

#include <boost/iterator/iterator_facade.hpp>

namespace dwarfs {

template <typename T, typename IndexValueType = size_t>
class sortable_span {
 public:
  using value_type = T;
  using index_value_type = IndexValueType;

  class iterator
      : public boost::iterator_facade<iterator, value_type,
                                      boost::random_access_traversal_tag> {
   public:
    using difference_type = typename boost::iterator_facade<
        iterator, value_type,
        boost::random_access_traversal_tag>::difference_type;

    iterator() = default;
    iterator(iterator const& other) = default;

   private:
    friend class boost::iterator_core_access;
    friend class sortable_span;

    iterator(sortable_span const* vv,
             typename std::vector<index_value_type>::iterator it)
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

    value_type& dereference() const { return vv_->values_[*it_]; }

    sortable_span const* vv_{nullptr};
    typename std::vector<index_value_type>::iterator it_;
  };

  explicit sortable_span(std::span<value_type> values)
      : values_{values} {}

  template <typename P>
  void select(P&& predicate) {
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

  std::span<value_type> raw() const { return values_; }

 private:
  std::vector<index_value_type> index_;
  std::span<value_type> const values_;
};

} // namespace dwarfs
