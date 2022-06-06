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
#include <functional>
#include <string>

#include <boost/system/error_code.hpp>
#include <boost/system/system_error.hpp>

namespace dwarfs {

class error : public std::exception {
 public:
  char const* what() const noexcept override { return what_.c_str(); }

  char const* file() const { return file_; }

  int line() const { return line_; }

 protected:
  error(std::string const& s, char const* file, int line) noexcept;

 private:
  std::string what_;
  char const* file_;
  int line_;
};

class runtime_error : public error {
 public:
  runtime_error(std::string const& s, char const* file, int line) noexcept;
};

class system_error : public boost::system::system_error {
 public:
  system_error(char const* file, int line) noexcept;
  system_error(std::string const& s, char const* file, int line) noexcept;
  system_error(std::string const& s, int err, char const* file,
               int line) noexcept;
  system_error(int err, char const* file, int line) noexcept;

  int get_errno() const { return code().value(); }

  char const* file() const { return file_; }

  int line() const { return line_; }

 private:
  char const* file_;
  int line_;
};

#define DWARFS_THROW(cls, ...) throw cls(__VA_ARGS__, __FILE__, __LINE__)

#define DWARFS_CHECK(expr, message)                                            \
  do {                                                                         \
    if (!(expr)) {                                                             \
      assertion_failed(#expr, message, __FILE__, __LINE__);                    \
    }                                                                          \
  } while (0)

#define DWARFS_NOTHROW(expr)                                                   \
  [&]() -> decltype(expr) {                                                    \
    try {                                                                      \
      return expr;                                                             \
    } catch (...) {                                                            \
      handle_nothrow(#expr, __FILE__, __LINE__);                               \
    }                                                                          \
  }()

void dump_exceptions();

[[noreturn]] void handle_nothrow(char const* expr, char const* file, int line);

[[noreturn]] void assertion_failed(char const* expr, std::string const& msg,
                                   char const* file, int line);

int safe_main(std::function<int(void)> fn);

} // namespace dwarfs
