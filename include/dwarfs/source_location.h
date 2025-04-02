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

// All this mess is necessary because of AppleClang missing <source_location>
// TODO: remove once AppleClang has caught up

#include <version>

#if __has_include(<source_location>) && defined(__cpp_lib_source_location) && __cpp_lib_source_location >= 201907L

#include <source_location>

namespace dwarfs {
using source_location = std::source_location;
} // namespace dwarfs

#define DWARFS_CURRENT_SOURCE_LOCATION ::dwarfs::source_location::current()

#elif __has_include(<experimental/source_location>)

#include <experimental/source_location>

namespace dwarfs {
using source_location = std::experimental::source_location;
} // namespace dwarfs

#define DWARFS_CURRENT_SOURCE_LOCATION ::dwarfs::source_location::current()

#else

#include <cstdint>

namespace dwarfs {

struct source_location {
  static constexpr source_location
  current(char const* file = "unknown", std::uint_least32_t line = 0,
          char const* func = "unknown") noexcept {
    return {file, line, func};
  }

  constexpr source_location() noexcept
      : source_location{current()} {}

  source_location(source_location const&) = default;
  source_location& operator=(source_location const&) = default;
  source_location(source_location&&) = default;
  source_location& operator=(source_location&&) = default;

  constexpr char const* file_name() const noexcept { return file_; }
  constexpr std::uint_least32_t line() const noexcept { return line_; }
  constexpr std::uint_least32_t column() const noexcept { return 0; }
  constexpr char const* function_name() const noexcept { return func_; }

 private:
  constexpr source_location(char const* file, std::uint_least32_t line,
                            char const* func) noexcept
      : file_{file}
      , line_{line}
      , func_{func} {}

  char const* file_;
  std::uint_least32_t line_;
  char const* func_;
};

} // namespace dwarfs

#define DWARFS_CURRENT_SOURCE_LOCATION                                         \
  ::dwarfs::source_location::current(__FILE__, __LINE__, __func__)

#endif
