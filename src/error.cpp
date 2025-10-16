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

#ifdef DWARFS_STACKTRACE_ENABLED
#if __has_include(<cpptrace/formatting.hpp>)
#include <cpptrace/formatting.hpp>
#define DWARFS_CPPTRACE_HAS_FORMATTING
#endif
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

void print_stacktrace(std::ostream& os [[maybe_unused]]) {
#ifdef DWARFS_STACKTRACE_ENABLED
  auto trace = cpptrace::generate_trace();
#ifdef DWARFS_CPPTRACE_HAS_FORMATTING
  auto formatter = cpptrace::formatter{}.addresses(
      cpptrace::formatter::address_mode::object);
  formatter.print(os, trace, true);
#else
  trace.print(os, true);
#endif
#endif
}

} // namespace

error::error(source_location loc) noexcept
    : loc_{loc}
#ifdef DWARFS_STACKTRACE_ENABLED
    , trace_{cpptrace::generate_raw_trace()}
#endif
{
}

#ifdef DWARFS_STACKTRACE_ENABLED
cpptrace::stacktrace error::stacktrace() const { return trace_.resolve(); }
#endif

runtime_error::runtime_error(std::string_view s, source_location loc)
    : error{loc}
    , what_{fmt::format("[{}:{}] {}", basename(loc.file_name()), loc.line(),
                        s)} {}

system_error::system_error(std::string_view s, source_location loc)
    : system_error(s, errno, loc) {}

system_error::system_error(std::string_view s, int err, source_location loc)
    : error{loc}
    , syserr_{err, std::generic_category(),
              fmt::format("[{}:{}] {}", basename(loc.file_name()), loc.line(),
                          s)} {}

void handle_nothrow(std::string_view expr, source_location loc) {
  std::cerr << "Expression `" << expr << "` threw `"
            << exception_str(std::current_exception()) << "` in "
            << loc.file_name() << "(" << loc.line() << ")\n";
  print_stacktrace(std::cerr);
  do_terminate();
}

void assertion_failed(std::string_view expr, std::string_view msg,
                      source_location loc) {
  std::cerr << "Assertion `" << expr << "` failed in " << loc.file_name() << "("
            << loc.line() << "): " << msg << "\n";
  print_stacktrace(std::cerr);
  do_terminate();
}

void handle_panic(std::string_view msg, source_location loc) {
  std::cerr << "Panic: " << msg << " in " << loc.file_name() << "("
            << loc.line() << ")\n";
  print_stacktrace(std::cerr);
  do_terminate();
}

} // namespace dwarfs
