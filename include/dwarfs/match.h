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

#include <type_traits>
#include <variant>

namespace dwarfs {

template <typename... Ts>
struct match : Ts... {
  using Ts::operator()...;
};

template <typename... Ts>
match(Ts...) -> match<Ts...>;

namespace detail {

template <typename T>
struct is_a_variant : std::false_type {};

template <typename... Ts>
struct is_a_variant<std::variant<Ts...>> : std::true_type {};

template <typename T>
struct is_a_match : std::false_type {};

template <typename... Ts>
struct is_a_match<match<Ts...>> : std::true_type {};

} // namespace detail

template <typename T, typename U>
[[nodiscard]] constexpr decltype(auto) operator|(T&& v, U&& m)
  requires(detail::is_a_variant<std::decay_t<T>>::value &&
           detail::is_a_match<std::decay_t<U>>::value)
{
  return std::visit(std::forward<U>(m), std::forward<T>(v));
}

} // namespace dwarfs
