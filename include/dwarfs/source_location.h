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

#include <cstdint>

namespace dwarfs {

// This version doesn't store the function name (which isn't being used
// anywhere, but for which the compiler still generates all the constant
// strings). This saves more than 100 KiB of constant data.

struct source_location {
  static constexpr source_location
  current(char const* file, std::uint_least32_t line) noexcept {
    return {file, line};
  }

  source_location(source_location const&) = default;
  source_location& operator=(source_location const&) = default;
  source_location(source_location&&) = default;
  source_location& operator=(source_location&&) = default;

  constexpr char const* file_name() const noexcept { return file_; }
  constexpr std::uint_least32_t line() const noexcept { return line_; }
  constexpr std::uint_least32_t column() const noexcept { return 0; }

 private:
  constexpr source_location(char const* file, std::uint_least32_t line) noexcept
      : file_{file}
      , line_{line} {}

  char const* file_;
  std::uint_least32_t line_;
};

} // namespace dwarfs

#define DWARFS_CURRENT_SOURCE_LOCATION                                         \
  ::dwarfs::source_location::current(__FILE__, __LINE__)
