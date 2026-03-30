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
#include <string>
#include <type_traits>
#include <utility>

#include <boost/convert.hpp>
#include <boost/convert/spirit.hpp>

#include <dwarfs/compiler.h>

namespace dwarfs {

namespace detail {

std::optional<bool> str_to_bool(std::string_view s);

} // namespace detail

class conversion_error : public std::runtime_error {
 public:
  conversion_error(std::string const& value, std::type_info const& from,
                   std::type_info const& to);

  template <typename U>
  conversion_error(U const& value, std::type_info const& to)
      : conversion_error(stringify(value), typeid(U), to) {}

 private:
  static std::string stringify(std::string_view s) { return std::string(s); }

  template <typename U>
    requires(std::is_arithmetic_v<std::decay_t<U>>)
  static std::string stringify(U const& value) {
    return std::to_string(value);
  }
};

template <typename T, typename U>
std::optional<T> try_to(U const& s)
  requires(!std::same_as<T, bool> && !std::convertible_to<U, T>)
{
  DWARFS_PUSH_WARNING
  DWARFS_GCC_DISABLE_WARNING("-Wmaybe-uninitialized")
  if (auto r = boost::convert<T>(s, boost::cnv::spirit())) {
    return r.value();
  }
  DWARFS_POP_WARNING
  return std::nullopt;
}

template <typename T, typename U>
std::optional<bool> try_to(U const& s)
  requires(std::same_as<T, bool> && std::is_arithmetic_v<U>)
{
  return s != U{};
}

template <typename T>
std::optional<bool> try_to(std::string_view s)
  requires std::same_as<T, bool>
{
  return detail::str_to_bool(s);
}

template <typename T, typename U>
std::optional<T> try_to(U const& s)
  requires(!std::same_as<T, bool> && std::convertible_to<U, T>)
{
  return s;
}

template <typename T, typename U>
T to(U const& s) {
  if constexpr (std::same_as<T, std::decay_t<U>>) {
    return s;
  } else {
    auto const r = try_to<T>(s);
    if (r) {
      return *r;
    }
    throw conversion_error(s, typeid(T));
  }
}

} // namespace dwarfs
