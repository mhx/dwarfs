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

#include <exception>
#include <string>
#include <string_view>
#include <system_error>

#include <dwarfs/config.h>

#ifdef DWARFS_STACKTRACE_ENABLED
#include <cpptrace/cpptrace.hpp>
#endif

#include <dwarfs/source_location.h>

namespace dwarfs {

class error : public std::exception {
 public:
  auto location() const { return loc_; }
  auto file() const { return loc_.file_name(); }
  auto line() const { return loc_.line(); }
#ifdef DWARFS_STACKTRACE_ENABLED
  cpptrace::stacktrace stacktrace() const;
#endif

 protected:
  error(source_location loc) noexcept;

 private:
  source_location loc_;
#ifdef DWARFS_STACKTRACE_ENABLED
  cpptrace::raw_trace trace_;
#endif
};

class runtime_error : public error {
 public:
  runtime_error(std::string_view s, source_location loc);

  char const* what() const noexcept override { return what_.c_str(); }

 private:
  std::string what_;
};

class system_error : public error {
 public:
  system_error(std::string_view s, source_location loc);
  system_error(std::string_view s, int err, source_location loc);

  char const* what() const noexcept override { return syserr_.what(); }
  std::error_code const& code() const noexcept { return syserr_.code(); }
  int get_errno() const { return code().value(); }

 private:
  std::system_error syserr_;
};

#define DWARFS_THROW(cls, ...)                                                 \
  throw cls(__VA_ARGS__, DWARFS_CURRENT_SOURCE_LOCATION)

#define DWARFS_CHECK(expr, message)                                            \
  do {                                                                         \
    if (!(expr)) {                                                             \
      ::dwarfs::assertion_failed(#expr, message,                               \
                                 DWARFS_CURRENT_SOURCE_LOCATION);              \
    }                                                                          \
  } while (false)

#define DWARFS_PANIC(message)                                                  \
  do {                                                                         \
    ::dwarfs::handle_panic(message, DWARFS_CURRENT_SOURCE_LOCATION);           \
  } while (false)

#define DWARFS_NOTHROW(expr)                                                   \
  [&]() -> decltype(expr) {                                                    \
    try {                                                                      \
      return expr;                                                             \
    } catch (...) {                                                            \
      ::dwarfs::handle_nothrow(#expr, DWARFS_CURRENT_SOURCE_LOCATION);         \
    }                                                                          \
  }()

[[noreturn]] void handle_nothrow(std::string_view expr, source_location loc);

[[noreturn]] void assertion_failed(std::string_view expr, std::string_view msg,
                                   source_location loc);

[[noreturn]] void handle_panic(std::string_view msg, source_location loc);

} // namespace dwarfs
