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
#include <compare>
#include <cstddef>
#include <iterator>
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>

namespace dwarfs::container::detail {

template <typename Container, bool IsConst>
class index_based_iterator_impl {
 public:
  using container_type = Container;
  using container_pointer =
      std::conditional_t<IsConst, container_type const*, container_type*>;
  using container_reference =
      std::conditional_t<IsConst, container_type const&, container_type&>;
  using size_type = typename container_type::size_type;
  using iterator_concept = std::random_access_iterator_tag;
  using iterator_category = std::random_access_iterator_tag;
  using value_type = typename container_type::value_type;
  using difference_type = std::ptrdiff_t;
  using reference =
      std::conditional_t<IsConst, typename container_type::const_reference,
                         typename container_type::reference>;
  using pointer =
      std::conditional_t<std::is_reference_v<reference>,
                         std::add_pointer_t<std::remove_reference_t<reference>>,
                         void>;

  index_based_iterator_impl() = default;

  template <bool IsConstSrc>
  index_based_iterator_impl(
      index_based_iterator_impl<container_type, IsConstSrc> const& it)
    requires(IsConst && !IsConstSrc)
      : vec_{it.vec_}
      , index_{it.index_} {
    assert(index_ >= 0);
  }

  static index_based_iterator_impl
  from_index(container_reference vec, size_type index) {
    return index_based_iterator_impl{&vec, index};
  }

  size_type get_index() const {
    assert(index_ >= 0);
    return static_cast<size_type>(index_);
  }

  bool belongs_to(container_reference vec) const { return vec_ == &vec; }

  reference operator*() const { return (*vec_)[get_index()]; }

  auto operator->() const -> pointer
    requires std::is_reference_v<reference>
  {
    return std::addressof((*vec_)[get_index()]);
  }

  reference operator[](difference_type n) const {
    auto tmp = *this;
    tmp += n;
    return *tmp;
  }

  index_based_iterator_impl& operator++() {
    ++index_;
    assert(index_ >= 0);
    return *this;
  }

  index_based_iterator_impl operator++(int) {
    auto tmp = *this;
    ++*this;
    return tmp;
  }

  index_based_iterator_impl& operator--() {
    --index_;
    assert(index_ >= 0);
    return *this;
  }

  index_based_iterator_impl operator--(int) {
    auto tmp = *this;
    --*this;
    return tmp;
  }

  index_based_iterator_impl& operator+=(difference_type n) {
    index_ += n;
    assert(index_ >= 0);
    return *this;
  }

  index_based_iterator_impl& operator-=(difference_type n) {
    return *this += -n;
  }

  friend index_based_iterator_impl
  operator+(index_based_iterator_impl it, difference_type n) {
    it += n;
    return it;
  }

  friend index_based_iterator_impl
  operator+(difference_type n, index_based_iterator_impl it) {
    it += n;
    return it;
  }

  friend index_based_iterator_impl
  operator-(index_based_iterator_impl it, difference_type n) {
    it -= n;
    return it;
  }

  friend difference_type
  operator-(index_based_iterator_impl a, index_based_iterator_impl b) {
    assert(a.vec_ == b.vec_);
    return a.index_ - b.index_;
  }

  friend bool
  operator==(index_based_iterator_impl a, index_based_iterator_impl b) {
    return a.vec_ == b.vec_ && a.index_ == b.index_;
  }

  friend auto
  operator<=>(index_based_iterator_impl a, index_based_iterator_impl b) {
    assert(a.vec_ == b.vec_);
    return a.index_ <=> b.index_;
  }

  friend std::remove_reference_t<reference>&&
  iter_move(index_based_iterator_impl const& it)
    requires std::is_reference_v<reference>
  {
    return std::move((*it.vec_)[it.get_index()]);
  }

  friend value_type iter_move(index_based_iterator_impl const& it)
    requires(!std::is_reference_v<reference>)
  {
    return static_cast<value_type>((*it.vec_)[it.get_index()]);
  }

  friend void iter_swap(index_based_iterator_impl const& a,
                        index_based_iterator_impl const& b)
      noexcept(std::is_nothrow_swappable_v<std::remove_reference_t<reference>>)
    requires(std::is_reference_v<reference> && !IsConst)
  {
    assert(a.vec_ == b.vec_);
    using std::swap;
    swap((*a.vec_)[a.get_index()], (*b.vec_)[b.get_index()]);
  }

  friend void iter_swap(index_based_iterator_impl const& a,
                        index_based_iterator_impl const& b)
      noexcept(std::is_nothrow_constructible_v<value_type, reference> &&
               std::is_nothrow_assignable_v<reference, value_type>)
    requires(!std::is_reference_v<reference> && !IsConst)
  {
    assert(a.vec_ == b.vec_);
    auto tmp = static_cast<value_type>((*a.vec_)[a.get_index()]);
    (*a.vec_)[a.get_index()] =
        static_cast<value_type>((*b.vec_)[b.get_index()]);
    (*b.vec_)[b.get_index()] = std::move(tmp);
  }

 private:
  friend index_based_iterator_impl<container_type, !IsConst>;

  index_based_iterator_impl(container_pointer vec, size_type index)
      : vec_{vec}
      , index_{to_index(index)} {
    assert(vec_);
    assert(index_ <= to_index(vec_->size()));
  }

  static difference_type to_index(size_type index) {
    assert(index <=
           static_cast<size_type>(std::numeric_limits<difference_type>::max()));
    return static_cast<difference_type>(index);
  }

  container_pointer vec_{nullptr};
  difference_type index_{0};
};

template <typename Container>
using index_based_iterator = index_based_iterator_impl<Container, false>;

template <typename Container>
using index_based_const_iterator = index_based_iterator_impl<Container, true>;

} // namespace dwarfs::container::detail
