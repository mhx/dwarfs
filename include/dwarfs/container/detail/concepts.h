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

#include <concepts>
#include <type_traits>
#include <utility>

namespace dwarfs::container::detail {

/**
 * A contiguous, sized range of byte-sized trivially copyable elements.
 *
 * Deliberately accepts std::byte, char, unsigned char, ... so that callers
 * can insert and look up using whatever byte container they already have
 * (std::span, std::vector<std::uint8_t>, std::string_view, std::array).
 */
template <typename R>
concept byte_range =
    std::ranges::contiguous_range<R> && std::ranges::sized_range<R> &&
    sizeof(std::ranges::range_value_t<R>) == 1 &&
    std::is_trivially_copyable_v<std::ranges::range_value_t<R>> &&
    !std::same_as<std::remove_cvref_t<std::ranges::range_value_t<R>>, bool>;

template <typename Op, typename Lhs, typename Rhs>
concept closed_under = requires(Op op) {
  { op(std::declval<Lhs>(), std::declval<Rhs>()) } -> std::convertible_to<Lhs>;
};

/**
 * A sized, random-access container addressed by index rather than by pointer.
 *
 * This is the interface required by `index_based_span_impl`. It is deliberately
 * limited to what a span actually needs: the nested types, the size, indexed
 * read/write access, and the ability to synthesize iterators for an index.
 *
 * Mutable access is part of the concept, so a hypothetical read-only container
 * would not satisfy it. Should that become relevant, this concept can be split
 * into a read-only base concept and a mutable refinement, with the span
 * requiring the latter only for `IsConst == false`.
 */
template <typename C>
concept index_based_container = requires(
    C& c, C const& cc, typename C::size_type i, typename C::value_type v) {
  typename C::size_type;
  typename C::value_type;
  typename C::reference;
  typename C::const_reference;
  typename C::iterator;
  typename C::const_iterator;
  { cc.size() } -> std::same_as<typename C::size_type>;
  { cc[i] } -> std::convertible_to<typename C::const_reference>;
  { c[i] } -> std::same_as<typename C::reference>;
  { cc.get(i) } -> std::convertible_to<typename C::const_reference>;
  c.set(i, v);
  { C::iterator::from_index(c, i) } -> std::same_as<typename C::iterator>;
  {
    C::const_iterator::from_index(cc, i)
  } -> std::same_as<typename C::const_iterator>;
};

} // namespace dwarfs::container::detail
