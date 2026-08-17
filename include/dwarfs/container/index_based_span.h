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

#include <memory>
#include <ranges>

#include <dwarfs/container/detail/index_based_span_impl.h>

namespace dwarfs::container {

/**
 * A mutable span over an index-based container.
 */
template <typename Container>
using index_based_span = detail::index_based_span_impl<Container, false>;

/**
 * A read-only span over an index-based container.
 */
template <typename Container>
using index_based_const_span = detail::index_based_span_impl<Container, true>;

/**
 * Create a span covering all elements of `vec`.
 *
 * These overloads are found by ADL for any container in this namespace that
 * satisfies `detail::index_based_container`, so `make_span(vec)` works without
 * naming the namespace and without the container knowing about spans.
 */
template <detail::index_based_container C>
[[nodiscard]] auto make_span(C& vec) -> index_based_span<C> {
  return index_based_span<C>{vec};
}

template <detail::index_based_container C>
[[nodiscard]] auto make_span(C const& vec) -> index_based_const_span<C> {
  return index_based_const_span<C>{vec};
}

/**
 * Create a span covering the elements of `vec` from `offset` to the end.
 */
template <detail::index_based_container C>
[[nodiscard]] auto
make_span(C& vec, typename C::size_type offset) -> index_based_span<C> {
  return index_based_span<C>{vec, offset};
}

template <detail::index_based_container C>
[[nodiscard]] auto make_span(C const& vec, typename C::size_type offset)
    -> index_based_const_span<C> {
  return index_based_const_span<C>{vec, offset};
}

/**
 * Create a span covering `count` elements of `vec`, starting at `offset`.
 */
template <detail::index_based_container C>
[[nodiscard]] auto
make_span(C& vec, typename C::size_type offset, typename C::size_type count)
    -> index_based_span<C> {
  return index_based_span<C>{vec, offset, count};
}

template <detail::index_based_container C>
[[nodiscard]] auto
make_span(C const& vec, typename C::size_type offset,
          typename C::size_type count) -> index_based_const_span<C> {
  return index_based_const_span<C>{vec, offset, count};
}

// Spans must not be built from temporary containers.
template <detail::index_based_container C>
void make_span(C const&&) = delete;

template <detail::index_based_container C>
void make_span(C const&&, typename C::size_type) = delete;

template <detail::index_based_container C>
void make_span(C const&&, typename C::size_type,
               typename C::size_type) = delete;

/**
 * Create a span from a pair of iterators into the same container.
 *
 * Both iterators must have the same constness; the resulting span is read-only
 * if and only if the iterators are. Mixed pairs such as
 * `(vec.begin(), vec.cend())` are deliberately not accepted.
 */
template <typename C, bool IsConst>
[[nodiscard]] auto
make_span(detail::index_based_iterator_impl<C, IsConst> first,
          detail::index_based_iterator_impl<C, IsConst> last)
    -> detail::index_based_span_impl<C, IsConst> {
  assert(std::addressof(first.container()) == std::addressof(last.container()));
  assert(first.get_index() <= last.get_index());

  return detail::index_based_span_impl<C, IsConst>{
      first.container(), first.get_index(),
      last.get_index() - first.get_index()};
}

/**
 * Create a span of `count` elements starting at `first`.
 */
template <typename C, bool IsConst>
[[nodiscard]] auto make_span(
    detail::index_based_iterator_impl<C, IsConst> first,
    typename detail::index_based_iterator_impl<C, IsConst>::size_type count)
    -> detail::index_based_span_impl<C, IsConst> {
  return detail::index_based_span_impl<C, IsConst>{first.container(),
                                                   first.get_index(), count};
}

} // namespace dwarfs::container

namespace std::ranges {

template <typename Container, bool IsConst>
constexpr inline bool enable_borrowed_range<
    dwarfs::container::detail::index_based_span_impl<Container, IsConst>> =
    true;

template <typename Container, bool IsConst>
constexpr inline bool enable_view<
    dwarfs::container::detail::index_based_span_impl<Container, IsConst>> =
    true;

} // namespace std::ranges
