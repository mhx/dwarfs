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

#include <cassert>
#include <cstddef>
#include <iterator>
#include <utility>

namespace dwarfs::container::detail {

template <typename Container>
class index_based_const_iterator;

template <typename Container>
class index_based_iterator {
 public:
  using container_type = Container;
  using size_type = typename container_type::size_type;
  using iterator_concept = std::random_access_iterator_tag;
  using iterator_category = std::random_access_iterator_tag;
  using value_type = Container::value_type;
  using difference_type = std::ptrdiff_t;
  using reference = Container::value_proxy;

  index_based_iterator() = default;

  reference operator*() const { return (*vec_)[index_]; }

  reference operator[](difference_type n) const { return (*vec_)[index_ + n]; }

  index_based_iterator& operator++() {
    ++index_;
    return *this;
  }

  index_based_iterator operator++(int) {
    auto tmp = *this;
    ++*this;
    return tmp;
  }

  index_based_iterator& operator--() {
    --index_;
    return *this;
  }

  index_based_iterator operator--(int) {
    auto tmp = *this;
    --*this;
    return tmp;
  }

  index_based_iterator& operator+=(difference_type n) {
    index_ += n;
    return *this;
  }

  index_based_iterator& operator-=(difference_type n) {
    index_ -= n;
    return *this;
  }

  friend index_based_iterator
  operator+(index_based_iterator it, difference_type n) {
    it += n;
    return it;
  }

  friend index_based_iterator
  operator+(difference_type n, index_based_iterator it) {
    it += n;
    return it;
  }

  friend index_based_iterator
  operator-(index_based_iterator it, difference_type n) {
    it -= n;
    return it;
  }

  friend difference_type
  operator-(index_based_iterator a, index_based_iterator b) {
    return static_cast<difference_type>(a.index_) -
           static_cast<difference_type>(b.index_);
  }

  friend bool operator==(index_based_iterator a, index_based_iterator b) {
    return a.vec_ == b.vec_ && a.index_ == b.index_;
  }

  friend auto operator<=>(index_based_iterator a, index_based_iterator b) {
    assert(a.vec_ == b.vec_);
    return a.index_ <=> b.index_;
  }

  friend value_type iter_move(index_based_iterator const& it) noexcept {
    return it.vec_->get(it.index_);
  }

  friend void iter_swap(index_based_iterator const& a,
                        index_based_iterator const& b) noexcept {
    using std::swap;
    swap((*a.vec_)[a.index_], (*b.vec_)[b.index_]);
  }

 private:
  friend container_type;
  friend index_based_const_iterator<container_type>;

  index_based_iterator(container_type* vec, size_type index)
      : vec_{vec}
      , index_{index} {}

  container_type* vec_{nullptr};
  size_type index_{0};
};

template <typename Container>
class index_based_const_iterator {
 public:
  using container_type = Container;
  using size_type = typename container_type::size_type;
  using iterator_concept = std::random_access_iterator_tag;
  using iterator_category = std::random_access_iterator_tag;
  using value_type = Container::value_type;
  using difference_type = std::ptrdiff_t;
  using reference = Container::value_type;

  index_based_const_iterator() = default;

  index_based_const_iterator(index_based_iterator<container_type> it)
      : vec_{it.vec_}
      , index_{it.index_} {}

  reference operator*() const { return (*vec_)[index_]; }

  reference operator[](difference_type n) const { return (*vec_)[index_ + n]; }

  index_based_const_iterator& operator++() {
    ++index_;
    return *this;
  }

  index_based_const_iterator operator++(int) {
    auto tmp = *this;
    ++*this;
    return tmp;
  }

  index_based_const_iterator& operator--() {
    --index_;
    return *this;
  }

  index_based_const_iterator operator--(int) {
    auto tmp = *this;
    --*this;
    return tmp;
  }

  index_based_const_iterator& operator+=(difference_type n) {
    index_ += n;
    return *this;
  }

  index_based_const_iterator& operator-=(difference_type n) {
    index_ -= n;
    return *this;
  }

  friend index_based_const_iterator
  operator+(index_based_const_iterator it, difference_type n) {
    it += n;
    return it;
  }

  friend index_based_const_iterator
  operator+(difference_type n, index_based_const_iterator it) {
    it += n;
    return it;
  }

  friend index_based_const_iterator
  operator-(index_based_const_iterator it, difference_type n) {
    it -= n;
    return it;
  }

  friend difference_type
  operator-(index_based_const_iterator a, index_based_const_iterator b) {
    return static_cast<difference_type>(a.index_) -
           static_cast<difference_type>(b.index_);
  }

  friend bool
  operator==(index_based_const_iterator a, index_based_const_iterator b) {
    return a.vec_ == b.vec_ && a.index_ == b.index_;
  }

  friend auto
  operator<=>(index_based_const_iterator a, index_based_const_iterator b) {
    assert(a.vec_ == b.vec_);
    return a.index_ <=> b.index_;
  }

  friend value_type iter_move(index_based_const_iterator const& it) noexcept {
    return it.vec_->get(it.index_);
  }

 private:
  friend container_type;

  index_based_const_iterator(container_type const* vec, size_type index)
      : vec_{vec}
      , index_{index} {}

  container_type const* vec_{nullptr};
  size_type index_{0};
};

} // namespace dwarfs::container::detail
