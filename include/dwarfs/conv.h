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
std::optional<T> tryTo(U&& s)
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
std::optional<bool> tryTo(U&& s)
  requires(std::same_as<T, bool> && std::is_arithmetic_v<U>)
{
  return s != U{};
}

template <typename T>
std::optional<bool> tryTo(std::string_view s)
  requires std::same_as<T, bool>
{
  return detail::str_to_bool(s);
}

template <typename T, typename U>
std::optional<T> tryTo(U&& s)
  requires(std::convertible_to<U, T>)
{
  return std::forward<U>(s);
}

template <typename T, typename U>
T to(U&& s) {
  if constexpr (std::same_as<T, std::decay_t<U>>) {
    return std::forward<U>(s);
  } else {
    return tryTo<T>(std::forward<U>(s)).value();
  }
}

} // namespace dwarfs
