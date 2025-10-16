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

#include <clocale>
#include <cstdlib>
#include <iostream>

#include <dwarfs/config.h>

#ifdef DWARFS_STACKTRACE_ENABLED
#include <cpptrace/from_current.hpp>
#if __has_include(<cpptrace/formatting.hpp>)
#include <cpptrace/formatting.hpp>
#define DWARFS_CPPTRACE_HAS_FORMATTING
#endif
#include <cpptrace/version.hpp>
#if CPPTRACE_VERSION < 10000
#define DWARFS_TRY CPPTRACE_TRYZ
#define DWARFS_CATCH CPPTRACE_CATCHZ
#else
#define DWARFS_TRY CPPTRACE_TRY
#define DWARFS_CATCH CPPTRACE_CATCH
#endif
#else
#define DWARFS_TRY try
#define DWARFS_CATCH catch
#endif

#include <dwarfs/error.h>
#include <dwarfs/tool/safe_main.h>
#include <dwarfs/util.h>

namespace dwarfs::tool {

int safe_main(std::function<int(void)> const& fn) {
  int retval{1};
  DWARFS_TRY {
    install_signal_handlers();
    setup_default_locale();
#ifdef _WIN32
    ::_set_abort_behavior(0, _WRITE_ABORT_MSG);
#endif

    retval = fn();
  }
  DWARFS_CATCH(...) {
    std::cerr << "ERROR: " << exception_str(std::current_exception()) << "\n";
#ifdef DWARFS_STACKTRACE_ENABLED
    auto const& stacktrace = cpptrace::from_current_exception();
#ifdef DWARFS_CPPTRACE_HAS_FORMATTING
    auto formatter = cpptrace::formatter{}.addresses(
        cpptrace::formatter::address_mode::object);
    formatter.print(std::cerr, stacktrace, true);
#else
    stacktrace.print(std::cerr, true);
#endif
#endif
  }
  return retval;
}

} // namespace dwarfs::tool
