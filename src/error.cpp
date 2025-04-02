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

#include <cerrno>
#include <clocale>
#include <cstdlib>
#include <iostream>

#include <fmt/format.h>

#include <dwarfs/config.h>

#ifdef DWARFS_USE_EXCEPTION_TRACER
#include <folly/experimental/exception_tracer/ExceptionTracer.h>
#endif

#include <dwarfs/error.h>
#include <dwarfs/terminal.h>
#include <dwarfs/util.h>

namespace dwarfs {

namespace {

[[noreturn]] void do_terminate() {
#ifdef DWARFS_COVERAGE_ENABLED
  std::exit(99);
#else
  std::abort();
#endif
}

} // namespace

error::error(std::string_view s, source_location loc) noexcept
    : what_{fmt::format("{} [{}:{}]", s, basename(loc.file_name()), loc.line())}
    , loc_{loc} {}

system_error::system_error(source_location loc) noexcept
    : system_error(errno, loc) {}

system_error::system_error(std::string_view s, source_location loc) noexcept
    : system_error(s, errno, loc) {}

system_error::system_error(std::string_view s, int err,
                           source_location loc) noexcept
    : std::system_error(err, std::generic_category(), std::string(s))
    , loc_{loc} {}

system_error::system_error(int err, source_location loc) noexcept
    : std::system_error(err, std::generic_category())
    , loc_{loc} {}

void dump_exceptions() {
#ifdef DWARFS_USE_EXCEPTION_TRACER
  auto exceptions = ::folly::exception_tracer::getCurrentExceptions();
  for (auto& exc : exceptions) {
    std::cerr << exc << "\n";
  }
#endif
}

void handle_nothrow(std::string_view expr, source_location loc) {
  std::cerr << "Expression `" << expr << "` threw `"
            << exception_str(std::current_exception()) << "` in "
            << loc.file_name() << "(" << loc.line() << ")\n";
  do_terminate();
}

void assertion_failed(std::string_view expr, std::string_view msg,
                      source_location loc) {
  std::cerr << "Assertion `" << expr << "` failed in " << loc.file_name() << "("
            << loc.line() << "): " << msg << "\n";
  do_terminate();
}

void handle_panic(std::string_view msg, source_location loc) {
  std::cerr << "Panic: " << msg << " in " << loc.file_name() << "("
            << loc.line() << ")\n";
  do_terminate();
}

} // namespace dwarfs
