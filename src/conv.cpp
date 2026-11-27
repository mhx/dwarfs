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

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <clocale>
#include <cstdlib>

#if __has_include(<xlocale.h>)
#include <xlocale.h>
#endif

#include <dwarfs/conv.h>
#include <dwarfs/thrift_lite/demangle.h>

namespace dwarfs {

conversion_error::conversion_error(std::string const& value,
                                   std::type_info const& from,
                                   std::type_info const& to)
    : std::runtime_error("could not convert `" + value + "' [" +
                         thrift_lite::demangle(from) + "] to " +
                         thrift_lite::demangle(to)) {}

namespace detail {

namespace {

bool eq_nocase(std::string_view const a, std::string_view const b) {
  return std::ranges::equal(a, b, [](char const a, char const b) {
    return std::tolower(a) == std::tolower(b);
  });
}

struct c_locale {
#ifdef _WIN32
  _locale_t value = ::_create_locale(LC_ALL, "C");

  ~c_locale() {
    if (value) {
      ::_free_locale(value);
    }
  }

  static constexpr auto parse_float = &::_strtof_l;
  static constexpr auto parse_double = &::_strtod_l;
  static constexpr auto parse_long_double = &::_strtold_l;
#else
  locale_t value = ::newlocale(LC_ALL_MASK, "C", nullptr);

  ~c_locale() {
    if (value) {
      ::freelocale(value);
    }
  }

  static constexpr auto parse_float = &::strtof_l;
  static constexpr auto parse_double = &::strtod_l;
  static constexpr auto parse_long_double = &::strtold_l;
#endif
};

auto get_c_locale() {
  static c_locale const loc;
  return loc.value;
}

} // namespace

std::optional<bool> str_to_bool(std::string_view s) {
  auto const len = s.size();

  if (len > 0) {
    switch (s.front()) {
    case '0':
    case '1':
      if (len == 1) {
        return s.front() == '1';
      }
      break;

    case 'f':
    case 'F':
      if (len == 1 || eq_nocase(s, "false")) {
        return false;
      }
      break;

    case 'n':
    case 'N':
      if (len == 1 || eq_nocase(s, "no")) {
        return false;
      }
      break;

    case 't':
    case 'T':
      if (len == 1 || eq_nocase(s, "true")) {
        return true;
      }
      break;

    case 'y':
    case 'Y':
      if (len == 1 || eq_nocase(s, "yes")) {
        return true;
      }
      break;

    default:
      break;
    }
  }

  return std::nullopt;
}

template <std::floating_point T>
std::optional<T> str_to_floating(std::string_view s) {
  if (s.empty() || s.find_first_of(" \t\n\r\f\v") == 0) {
    return std::nullopt;
  }

  auto magnitude = s;

  if (magnitude.front() == '+' || magnitude.front() == '-') {
    magnitude.remove_prefix(1);
  }

  if (magnitude.starts_with("0x") || magnitude.starts_with("0X")) {
    return std::nullopt;
  }

  auto const loc = get_c_locale();

  if (!loc) {
    return std::nullopt;
  }

  std::string const input(s);
  char* end = nullptr;

  errno = 0;

  T value;

  if constexpr (std::same_as<T, float>) {
    value = c_locale::parse_float(input.c_str(), &end, loc);
  } else if constexpr (std::same_as<T, double>) {
    value = c_locale::parse_double(input.c_str(), &end, loc);
  } else {
    value = c_locale::parse_long_double(input.c_str(), &end, loc);
  }

  if (errno != ERANGE && end != input.c_str() &&
      end == input.c_str() + input.size()) {
    return value;
  }

  return std::nullopt;
}

template std::optional<float> str_to_floating<float>(std::string_view);

template std::optional<double> str_to_floating<double>(std::string_view);

template std::optional<long double>
    str_to_floating<long double>(std::string_view);

} // namespace detail
} // namespace dwarfs
