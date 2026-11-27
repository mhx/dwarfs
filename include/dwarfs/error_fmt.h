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

#include <type_traits>

#include <fmt/format.h>

#include <dwarfs/compiler.h>
#include <dwarfs/error.h>

namespace dwarfs::detail {

template <typename Format>
inline fmt::string_view format_string_view(Format const& format) {
  if constexpr (requires { format.get(); }) {
    return format.get();
  } else {
    return format;
  }
}

[[noreturn]] DWARFS_NOINLINE DWARFS_COLD void
throw_formatted_impl(std::type_identity<runtime_error>, source_location loc,
                     fmt::string_view format, fmt::format_args args);

[[noreturn]] DWARFS_NOINLINE DWARFS_COLD void
assertion_failed_formatted_impl(std::string_view expr, source_location loc,
                                fmt::string_view format, fmt::format_args args);

template <typename Exception, typename... Args>
[[noreturn]] inline void
throw_formatted(source_location loc, fmt::format_string<Args...> format,
                Args&&... args) {
  throw_formatted_impl(std::type_identity<Exception>{}, loc,
                       format_string_view(format),
                       fmt::make_format_args(args...));
}

template <typename... Args>
[[noreturn]] inline void
assertion_failed_formatted(std::string_view expr, source_location loc,
                           fmt::format_string<Args...> format, Args&&... args) {
  assertion_failed_formatted_impl(expr, loc, format_string_view(format),
                                  fmt::make_format_args(args...));
}

} // namespace dwarfs::detail

// Currently only supports dwarfs::runtime_error.
#define DWARFS_THROWF(cls, ...)                                                \
  ::dwarfs::detail::throw_formatted<cls>(DWARFS_CURRENT_SOURCE_LOCATION,       \
                                         __VA_ARGS__)

#define DWARFS_CHECKF(expr, ...)                                               \
  do {                                                                         \
    if (!(expr)) {                                                             \
      ::dwarfs::detail::assertion_failed_formatted(                            \
          #expr, DWARFS_CURRENT_SOURCE_LOCATION, __VA_ARGS__);                 \
    }                                                                          \
  } while (false)
