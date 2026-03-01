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
#include <cstdint>
#include <iterator>
#include <type_traits>
#include <utility>

#include <dwarfs/thrift_lite/writer_options.h>

namespace dwarfs::thrift_lite::detail {

template <typename T>
concept writeable_type = requires(T const& v, writer_options const& opts) {
  { v.has_any_fields_for_write(opts) } -> std::same_as<bool>;
};

template <typename T>
concept collection_type = requires(T const& v) {
  typename T::value_type;
  { v.empty() } -> std::same_as<bool>;
};

template <typename T>
concept basic_type = std::is_integral_v<T> || std::is_enum_v<T>;

template <typename T>
concept enumeration_type = std::is_enum_v<T>;

template <typename T>
concept reservable_container_type = requires(T& v, typename T::size_type n) {
  { v.reserve(n) } -> std::same_as<void>;
};

template <typename T>
concept emplaceable_map_type =
    requires(T& v, typename T::key_type k, typename T::mapped_type m) {
      {
        v.emplace(k, m)
      } -> std::same_as<std::pair<typename T::iterator, bool>>;
    };

template <typename T>
concept byte_like_type =
    std::is_same_v<T, std::byte> ||
    (std::is_integral_v<T> && sizeof(T) == 1 && !std::same_as<T, bool>);

template <typename It>
concept byte_input_iterator =
    std::input_iterator<It> && byte_like_type<std::iter_value_t<It>>;

} // namespace dwarfs::thrift_lite::detail
