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

#include <array>
#include <cstddef>
#include <string_view>

namespace dwarfs::thrift_lite {

namespace detail {

template <typename T>
consteval std::string_view raw_type_name() {
#if defined(__clang__) || defined(__GNUC__)
  return __PRETTY_FUNCTION__;
#elif defined(_MSC_VER)
  return __FUNCSIG__;
#else
#error "no way to obtain a type name on this compiler"
#endif
}

/**
 * clang: std::string_view dwarfs::detail::raw_type_name() [T = int]
 * gcc:   consteval std::string_view dwarfs::detail::raw_type_name()
 *          [with T = int; std::string_view = std::basic_string_view<char>]
 * msvc:  class std::basic_string_view<...> __cdecl
 *          dwarfs::detail::raw_type_name<int>(void)
 *
 * Rather than parse each dialect, measure the fixed prefix and suffix against
 * a probe type whose name we already know. This survives format changes as
 * long as the substitution is still a plain textual one.
 */
consteval std::string_view slice_type_name(std::string_view raw) {
  constexpr std::string_view probe_raw = [] { return raw_type_name<int>(); }();
  constexpr auto prefix = probe_raw.find("int");
  constexpr auto suffix = probe_raw.size() - prefix - 3;

  return raw.substr(prefix, raw.size() - prefix - suffix);
}

template <typename T>
struct type_name_holder {
  static constexpr auto storage = [] {
    constexpr auto name = slice_type_name(raw_type_name<T>());
    std::array<char, name.size() + 1> buf{};
    for (std::size_t i = 0; i < name.size(); ++i) {
      buf[i] = name[i];
    }
    return buf;
  }();
};

} // namespace detail

template <typename T>
constexpr inline std::string_view type_name = [] {
  return std::string_view{detail::type_name_holder<T>::storage.data(),
                          detail::type_name_holder<T>::storage.size() - 1};
}();

namespace detail {

struct type_name_probe {};

// slicing works: stick to simple types to avoid compiler specifics
static_assert(type_name<int> == "int");
static_assert(type_name<char> == "char");
static_assert(type_name<double> == "double");

// also works for user-defined types
static_assert(type_name<type_name_probe>.ends_with("type_name_probe"));

} // namespace detail
} // namespace dwarfs::thrift_lite
