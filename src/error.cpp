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

error::error(std::string_view s, std::source_location loc) noexcept
    : what_{fmt::format("{} [{}:{}]", s, basename(loc.file_name()), loc.line())}
    , loc_{loc} {}

system_error::system_error(std::source_location loc) noexcept
    : system_error(errno, loc) {}

system_error::system_error(std::string_view s,
                           std::source_location loc) noexcept
    : system_error(s, errno, loc) {}

system_error::system_error(std::string_view s, int err,
                           std::source_location loc) noexcept
    : std::system_error(err, std::generic_category(), std::string(s))
    , loc_{loc} {}

system_error::system_error(int err, std::source_location loc) noexcept
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

void handle_nothrow(std::string_view expr, std::source_location loc) {
  std::cerr << "Expression `" << expr << "` threw `"
            << exception_str(std::current_exception()) << "` in "
            << loc.file_name() << "(" << loc.line() << ")\n";
  do_terminate();
}

void assertion_failed(std::string_view expr, std::string_view msg,
                      std::source_location loc) {
  std::cerr << "Assertion `" << expr << "` failed in " << loc.file_name() << "("
            << loc.line() << "): " << msg << "\n";
  do_terminate();
}

} // namespace dwarfs
