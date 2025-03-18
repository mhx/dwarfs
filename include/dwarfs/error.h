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
#include <string>
#include <string_view>
#include <system_error>

#include <dwarfs/source_location.h>

namespace dwarfs {

class error : public std::exception {
 public:
  char const* what() const noexcept override { return what_.c_str(); }

  auto location() const { return loc_; }
  auto file() const { return loc_.file_name(); }
  auto line() const { return loc_.line(); }

 protected:
  error(std::string_view s, source_location loc) noexcept;

 private:
  std::string what_;
  source_location loc_;
};

class runtime_error : public error {
 public:
  runtime_error(std::string_view s, source_location loc) noexcept
      : error(s, loc) {}
};

class system_error : public std::system_error {
 public:
  system_error(source_location loc) noexcept;
  system_error(std::string_view s, source_location loc) noexcept;
  system_error(std::string_view s, int err, source_location loc) noexcept;
  system_error(int err, source_location loc) noexcept;

  auto get_errno() const { return code().value(); }

  auto location() const { return loc_; }
  auto file() const { return loc_.file_name(); }
  auto line() const { return loc_.line(); }

 private:
  source_location loc_;
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

void dump_exceptions();

[[noreturn]] void handle_nothrow(std::string_view expr, source_location loc);

[[noreturn]] void assertion_failed(std::string_view expr, std::string_view msg,
                                   source_location loc);

[[noreturn]] void handle_panic(std::string_view msg, source_location loc);

} // namespace dwarfs
