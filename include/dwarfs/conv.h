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
#include <optional>
#include <type_traits>
#include <utility>

#include <boost/convert.hpp>
#include <boost/convert/spirit.hpp>

namespace dwarfs {

namespace detail {

std::optional<bool> str_to_bool(std::string_view s);

} // namespace detail

template <typename T, typename U>
std::optional<T> try_to(U&& s)
  requires(!std::same_as<T, bool> && !std::convertible_to<U, T>)
{
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
  if (auto r = boost::convert<T>(std::forward<U>(s), boost::cnv::spirit())) {
    return r.value();
  }
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
  return std::nullopt;
}

template <typename T, typename U>
std::optional<bool> try_to(U&& s)
  requires(std::same_as<T, bool> && std::is_arithmetic_v<U>)
{
  return std::forward<U>(s) != U{};
}

template <typename T>
std::optional<bool> try_to(std::string_view s)
  requires std::same_as<T, bool>
{
  return detail::str_to_bool(s);
}

template <typename T, typename U>
std::optional<T> try_to(U&& s)
  requires(std::convertible_to<U, T>)
{
  return std::forward<U>(s);
}

template <typename T, typename U>
T to(U&& s) {
  if constexpr (std::same_as<T, std::decay_t<U>>) {
    return std::forward<U>(s);
  } else {
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    return try_to<T>(std::forward<U>(s)).value(); // throw if conversion fails
  }
}

} // namespace dwarfs
