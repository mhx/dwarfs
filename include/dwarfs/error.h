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

#pragma once

#include <exception>
#include <source_location>
#include <string>
#include <string_view>
#include <system_error>

namespace dwarfs {

class error : public std::exception {
 public:
  char const* what() const noexcept override { return what_.c_str(); }

  auto location() const { return loc_; }
  auto file() const { return loc_.file_name(); }
  auto line() const { return loc_.line(); }

 protected:
  error(std::string_view s, std::source_location loc) noexcept;

 private:
  std::string what_;
  std::source_location loc_;
};

class runtime_error : public error {
 public:
  runtime_error(std::string_view s, std::source_location loc) noexcept
      : error(s, loc) {}
};

class system_error : public std::system_error {
 public:
  system_error(std::source_location loc) noexcept;
  system_error(std::string_view s, std::source_location loc) noexcept;
  system_error(std::string_view s, int err, std::source_location loc) noexcept;
  system_error(int err, std::source_location loc) noexcept;

  auto get_errno() const { return code().value(); }

  auto location() const { return loc_; }
  auto file() const { return loc_.file_name(); }
  auto line() const { return loc_.line(); }

 private:
  std::source_location loc_;
};

#define DWARFS_THROW(cls, ...)                                                 \
  throw cls(__VA_ARGS__, std::source_location::current())

#define DWARFS_CHECK(expr, message)                                            \
  do {                                                                         \
    if (!(expr)) {                                                             \
      assertion_failed(#expr, message, std::source_location::current());       \
    }                                                                          \
  } while (0)

#define DWARFS_NOTHROW(expr)                                                   \
  [&]() -> decltype(expr) {                                                    \
    try {                                                                      \
      return expr;                                                             \
    } catch (...) {                                                            \
      handle_nothrow(#expr, std::source_location::current());                  \
    }                                                                          \
  }()

void dump_exceptions();

[[noreturn]] void
handle_nothrow(std::string_view expr, std::source_location loc);

[[noreturn]] void
assertion_failed(std::string_view expr, std::string_view message,
                 std::source_location loc);

} // namespace dwarfs
