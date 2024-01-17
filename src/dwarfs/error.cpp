/* vim:set ts=2 sw=2 sts=2 et: */
/**
 * \author     Marcus Holland-Moritz (github@mhxnet.de)
 * \copyright  Copyright (c) Marcus Holland-Moritz
 *
 * This file is part of dwarfs.
 *
 * dwarfs is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * dwarfs is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with dwarfs.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <cerrno>
#include <clocale>
#include <cstdlib>
#include <iostream>

#include <fmt/format.h>

#include <folly/String.h>

#ifdef DWARFS_USE_EXCEPTION_TRACER
#include <folly/experimental/exception_tracer/ExceptionTracer.h>
#endif

#include "dwarfs/error.h"
#include "dwarfs/terminal.h"
#include "dwarfs/util.h"

namespace dwarfs {

namespace {

[[noreturn]] void do_terminate() {
#ifdef DWARFS_COVERAGE_ENABLED
  std::exit(99);
#else
  std::terminate();
#endif
}

} // namespace

error::error(std::string const& s, char const* file, int line) noexcept
    : what_{fmt::format("{} [{}:{}]", s, basename(file), line)}
    , file_{file}
    , line_{line} {}

system_error::system_error(char const* file, int line) noexcept
    : system_error(errno, file, line) {}

system_error::system_error(std::string const& s, char const* file,
                           int line) noexcept
    : system_error(s, errno, file, line) {}

system_error::system_error(std::string const& s, int err, char const* file,
                           int line) noexcept
    : std::system_error(err, std::generic_category(), s.c_str())
    , file_(file)
    , line_(line) {}

system_error::system_error(int err, char const* file, int line) noexcept
    : std::system_error(err, std::generic_category())
    , file_(file)
    , line_(line) {}

void dump_exceptions() {
#ifdef DWARFS_USE_EXCEPTION_TRACER
  auto exceptions = ::folly::exception_tracer::getCurrentExceptions();
  for (auto& exc : exceptions) {
    std::cerr << exc << "\n";
  }
#else
  std::cerr << "cannot dump exceptions\n";
#endif
}

void handle_nothrow(char const* expr, char const* file, int line) {
  std::cerr << "Expression `" << expr << "` threw `"
            << folly::exceptionStr(std::current_exception()) << "` in " << file
            << "(" << line << ")\n";
  do_terminate();
}

void assertion_failed(char const* expr, std::string const& msg,
                      char const* file, int line) {
  std::cerr << "Assertion `" << expr << "` failed in " << file << "(" << line
            << "): " << msg << "\n";
  do_terminate();
}

} // namespace dwarfs
