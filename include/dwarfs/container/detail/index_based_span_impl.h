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
#include <stdexcept>
#include <type_traits>
#include <vector>

#include <dwarfs/container/detail/concepts.h>
#include <dwarfs/container/detail/index_based_iterator.h>

namespace dwarfs::container::detail {

/**
 * A `std::span`-like view for index-based containers.
 *
 * Unlike `std::span`, this cannot abstract away the container type: element
 * access goes through the container, so the container type is part of the
 * span type. What it does provide is a single object that replaces a pair
 * of iterators.
 *
 * The span stores a container pointer plus an offset and a count. As a
 * consequence, it stays valid across operations that merely relocate or
 * repack the storage of the referenced container. Operations that change
 * the logical contents (size or element order) should be treated as
 * invalidating all spans, in the same way they invalidate iterators.
 *
 * Iterators of a span are the iterators of the underlying container, so they
 * mix freely with iterators obtained from the container itself and can be
 * passed to container member functions taking iterators.
 */
template <index_based_container Container, bool IsConst>
class index_based_span_impl {
 public:
  using container_type = Container;
  using container_pointer =
      std::conditional_t<IsConst, container_type const*, container_type*>;
  using container_reference =
      std::conditional_t<IsConst, container_type const&, container_type&>;
  using size_type = typename container_type::size_type;
  using difference_type = std::ptrdiff_t;
  using value_type = typename container_type::value_type;
  using reference =
      std::conditional_t<IsConst, typename container_type::const_reference,
                         typename container_type::reference>;
  using const_reference = typename container_type::const_reference;
  using iterator =
      std::conditional_t<IsConst, typename container_type::const_iterator,
                         typename container_type::iterator>;
  using const_iterator = typename container_type::const_iterator;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  index_based_span_impl() = default;

  index_based_span_impl(container_reference vec)
      : index_based_span_impl{vec, 0, vec.size()} {}

  index_based_span_impl(container_reference vec, size_type offset)
      : index_based_span_impl{vec, offset, count_to_end(vec, offset)} {}

  index_based_span_impl(container_reference vec, size_type offset,
                        size_type count)
      : vec_{&vec}
      , offset_{offset}
      , count_{count} {
    assert(offset <= vec.size());
    assert(count <= vec.size() - offset);
  }

  // A span references the container for its whole lifetime, so binding to a
  // temporary is always a bug, even for a read-only span.
  index_based_span_impl(container_type const&&) = delete;
  index_based_span_impl(container_type const&&, size_type) = delete;
  index_based_span_impl(container_type const&&, size_type, size_type) = delete;

  template <bool IsConstSrc>
  index_based_span_impl(
      index_based_span_impl<container_type, IsConstSrc> const& other)
    requires(IsConst && !IsConstSrc)
      : vec_{other.vec_}
      , offset_{other.offset_}
      , count_{other.count_} {}

  [[nodiscard]] container_reference container() const {
    assert(vec_);
    return *vec_;
  }

  [[nodiscard]] size_type size() const noexcept { return count_; }

  [[nodiscard]] bool empty() const noexcept { return count_ == 0; }

  reference operator[](size_type i) const {
    assert(i < count_);
    return container()[absolute_index(i)];
  }

  reference at(size_type i) const {
    if (i >= count_) {
      throw std::out_of_range("index_based_span::at");
    }
    return (*this)[i];
  }

  [[nodiscard]] reference front() const {
    assert(!empty());
    return (*this)[0];
  }

  [[nodiscard]] reference back() const {
    assert(!empty());
    return (*this)[count_ - 1];
  }

  [[nodiscard]] const_reference get(size_type i) const {
    assert(i < count_);
    return container().get(absolute_index(i));
  }

  void set(size_type i, value_type value) const
    requires(!IsConst)
  {
    assert(i < count_);
    container().set(absolute_index(i), value);
  }

  [[nodiscard]] iterator begin() const { return make_iterator<iterator>(0); }

  [[nodiscard]] iterator end() const { return make_iterator<iterator>(count_); }

  [[nodiscard]] const_iterator cbegin() const {
    return make_iterator<const_iterator>(0);
  }

  [[nodiscard]] const_iterator cend() const {
    return make_iterator<const_iterator>(count_);
  }

  [[nodiscard]] reverse_iterator rbegin() const {
    return reverse_iterator{end()};
  }

  [[nodiscard]] reverse_iterator rend() const {
    return reverse_iterator{begin()};
  }

  [[nodiscard]] const_reverse_iterator crbegin() const {
    return const_reverse_iterator{cend()};
  }

  [[nodiscard]] const_reverse_iterator crend() const {
    return const_reverse_iterator{cbegin()};
  }

  [[nodiscard]] index_based_span_impl first(size_type count) const {
    assert(count <= count_);
    return subspan(0, count);
  }

  [[nodiscard]] index_based_span_impl last(size_type count) const {
    assert(count <= count_);
    return subspan(count_ - count, count);
  }

  [[nodiscard]] index_based_span_impl
  subspan(size_type offset, size_type count = std::dynamic_extent) const {
    assert(offset <= count_);

    auto const n = count == std::dynamic_extent ? count_ - offset : count;

    assert(n <= count_ - offset);

    return index_based_span_impl{vec_, offset_ + offset, n};
  }

  [[nodiscard]] std::vector<value_type> unpack() const {
    std::vector<value_type> result;
    result.reserve(count_);
    for (size_type i = 0; i < count_; ++i) {
      result.push_back(get(i));
    }
    return result;
  }

 private:
  friend index_based_span_impl<container_type, !IsConst>;

  index_based_span_impl(container_pointer vec, size_type offset,
                        size_type count) noexcept
      : vec_{vec}
      , offset_{offset}
      , count_{count} {}

  [[nodiscard]] static size_type
  count_to_end(container_reference vec, size_type offset) {
    assert(offset <= vec.size());
    return vec.size() - offset;
  }

  [[nodiscard]] size_type absolute_index(size_type i) const noexcept {
    return offset_ + i;
  }

  // A default-constructed span is a well-behaved empty range rather than a
  // range whose iterators must not be formed.
  template <typename It>
  [[nodiscard]] It make_iterator(size_type i) const {
    if (!vec_) {
      return It{};
    }
    return It::from_index(*vec_, absolute_index(i));
  }

  container_pointer vec_{nullptr};
  size_type offset_{0};
  size_type count_{0};
};

} // namespace dwarfs::container::detail
