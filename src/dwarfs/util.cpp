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

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdlib>

#if __has_include(<utf8cpp/utf8.h>)
#include <utf8cpp/utf8.h>
#else
#include <utf8.h>
#endif

#include <date/date.h>

#include <folly/Conv.h>
#include <folly/String.h>

#include "dwarfs/error.h"
#include "dwarfs/options.h"
#include "dwarfs/util.h"

extern "C" int dwarfs_wcwidth(int ucs);

namespace dwarfs {

namespace {

inline std::string trimmed(std::string in) {
  while (!in.empty() && in.back() == ' ') {
    in.pop_back();
  }
  return in;
}

template <typename T>
int call_sys_main_iolayer_impl(std::span<T> args, iolayer const& iol,
                               int (*main)(int, sys_char**, iolayer const&)) {
  std::vector<sys_string> argv;
  std::vector<sys_char*> argv_ptrs;
  argv.reserve(args.size());
  argv_ptrs.reserve(args.size());
  for (auto const& arg : args) {
    argv.emplace_back(string_to_sys_string(std::string(arg)));
    argv_ptrs.emplace_back(argv.back().data());
  }
  return main(argv_ptrs.size(), argv_ptrs.data(), iol);
}

} // namespace

std::string size_with_unit(size_t size) {
  return trimmed(folly::prettyPrint(size, folly::PRETTY_BYTES_IEC, true));
}

std::string time_with_unit(double sec) {
  return trimmed(folly::prettyPrint(sec, folly::PRETTY_TIME_HMS, false));
}

std::string time_with_unit(std::chrono::nanoseconds ns) {
  return time_with_unit(1e-9 * ns.count());
}

size_t parse_size_with_unit(std::string const& str) {
  size_t value;
  auto [ptr, ec]{std::from_chars(str.data(), str.data() + str.size(), value)};

  if (ec != std::errc()) {
    DWARFS_THROW(runtime_error, "cannot parse size value");
  }

  if (ptr[0] == '\0') {
    return value;
  }

  if (ptr[1] == '\0') {
    switch (ptr[0]) {
    case 't':
    case 'T':
      value <<= 10;
      [[fallthrough]];
    case 'g':
    case 'G':
      value <<= 10;
      [[fallthrough]];
    case 'm':
    case 'M':
      value <<= 10;
      [[fallthrough]];
    case 'k':
    case 'K':
      value <<= 10;
      return value;
    default:
      break;
    }
  }

  DWARFS_THROW(runtime_error, "unsupported size suffix");
}

std::chrono::milliseconds parse_time_with_unit(std::string const& str) {
  uint64_t value;
  auto [ptr, ec]{std::from_chars(str.data(), str.data() + str.size(), value)};

  if (ec != std::errc()) {
    DWARFS_THROW(runtime_error, "cannot parse time value");
  }

  switch (ptr[0]) {
  case 'h':
    if (ptr[1] == '\0') {
      return std::chrono::hours(value);
    }
    break;

  case 'm':
    if (ptr[1] == '\0') {
      return std::chrono::minutes(value);
    } else if (ptr[1] == 's' && ptr[2] == '\0') {
      return std::chrono::milliseconds(value);
    }
    break;

  case 's':
    if (ptr[1] != '\0') {
      break;
    }
    [[fallthrough]];
  case '\0':
    return std::chrono::seconds(value);

  default:
    break;
  }

  DWARFS_THROW(runtime_error, "unsupported time suffix");
}

std::chrono::system_clock::time_point parse_time_point(std::string const& str) {
  static constexpr std::array<char const*, 9> formats{
      "%Y%m%dT%H%M%S", "%Y%m%dT%H%M", "%Y%m%dT", "%F %T", "%FT%T",
      "%F %R",         "%FT%R",       "%FT",     "%F"};

  for (auto const& fmt : formats) {
    std::istringstream iss(str);
    std::chrono::system_clock::time_point tp;
    date::from_stream(iss, fmt, tp);
    if (!iss.fail()) {
      iss.peek();
      if (iss.eof()) {
        return tp;
      }
    }
  }

  DWARFS_THROW(runtime_error, "cannot parse time point");
}

file_off_t parse_image_offset(std::string const& str) {
  if (str == "auto") {
    return filesystem_options::IMAGE_OFFSET_AUTO;
  }

  auto off = folly::tryTo<file_off_t>(str);

  if (!off) {
    auto ce = folly::makeConversionError(off.error(), str);
    DWARFS_THROW(runtime_error,
                 fmt::format("failed to parse image offset: {} ({})", str,
                             folly::exceptionStr(ce)));
  }

  if (off.value() < 0) {
    DWARFS_THROW(runtime_error, "image offset must be positive");
  }

  return off.value();
}

std::string sys_string_to_string(sys_string const& in) {
#ifdef _WIN32
  std::u16string tmp(in.size(), 0);
  std::transform(in.begin(), in.end(), tmp.begin(),
                 [](sys_char c) { return static_cast<char16_t>(c); });
  return utf8::utf16to8(tmp);
#else
  return in;
#endif
}

sys_string string_to_sys_string(std::string const& in) {
#ifdef _WIN32
  auto tmp = utf8::utf8to16(in);
  sys_string rv(tmp.size(), 0);
  std::transform(tmp.begin(), tmp.end(), rv.begin(),
                 [](char16_t c) { return static_cast<sys_char>(c); });
  return rv;
#else
  return in;
#endif
}

int call_sys_main_iolayer(std::span<std::string_view> args, iolayer const& iol,
                          int (*main)(int, sys_char**, iolayer const&)) {
  return call_sys_main_iolayer_impl(args, iol, main);
}

int call_sys_main_iolayer(std::span<std::string> args, iolayer const& iol,
                          int (*main)(int, sys_char**, iolayer const&)) {
  return call_sys_main_iolayer_impl(args, iol, main);
}

size_t utf8_display_width(char const* p, size_t len) {
  char const* const e = p + len;
  size_t rv = 0;

  while (p < e) {
    auto cp = utf8::next(p, e);
    rv += dwarfs_wcwidth(cp);
  }

  return rv;
}

size_t utf8_display_width(std::string const& str) {
  return utf8_display_width(str.data(), str.size());
}

void utf8_truncate(std::string& str, size_t len) {
  char const* p = str.data();
  char const* const e = p + str.size();
  size_t l = 0;

  while (p < e && l <= len) {
    auto np = p;
    auto cp = utf8::next(np, e);
    l += dwarfs_wcwidth(cp);
    if (l > len) {
      break;
    }
    p = np;
  }

  str.resize(p - str.data());
}

void shorten_path_string(std::string& path, char separator, size_t max_len) {
  auto len = utf8_display_width(path);

  if (len > max_len) {
    if (max_len < 3) {
      path.clear();
      return;
    }

    size_t start = 0;

    while (utf8_display_width(path.data() + start, path.size() - start) >
           max_len - 3) {
      auto next = path.find(separator, start + 1);
      if (next == std::string::npos) {
        break;
      }
      start = next;
    }

    path.replace(0, start, "...");

    if (auto len = utf8_display_width(path); len > max_len) {
      if (max_len >= 7) {
        utf8_truncate(path, max_len - 3);
        path += "...";
      } else {
        path = "...";
      }
    }
  }
}

std::filesystem::path canonical_path(std::filesystem::path p) {
  try {
    p = std::filesystem::canonical(p);
  } catch (std::filesystem::filesystem_error const&) {
    p = std::filesystem::absolute(p);
  }

#ifdef _WIN32
  p = std::filesystem::path(L"\\\\?\\" + p.wstring());
#endif

  return p;
}

bool getenv_is_enabled(char const* var) {
  if (auto val = std::getenv(var)) {
    if (auto maybeBool = folly::tryTo<bool>(val); maybeBool && *maybeBool) {
      return true;
    }
  }
  return false;
}

void setup_default_locale() {
  try {
#ifdef _WIN32
    char const* locale = "en_US.utf8";
#else
    char const* locale = "";
#endif
    std::locale::global(std::locale(locale));
    if (!std::setlocale(LC_ALL, locale)) {
      std::cerr << "warning: setlocale(LC_ALL, \"\") failed\n";
    }
  } catch (std::exception const& e) {
    std::cerr << "warning: failed to set user default locale: " << e.what()
              << "\n";
    try {
      std::locale::global(std::locale::classic());
      if (!std::setlocale(LC_ALL, "C")) {
        std::cerr << "warning: setlocale(LC_ALL, \"C\") failed\n";
      }
    } catch (std::exception const& e) {
      std::cerr << "warning: also failed to set classic locale: " << e.what()
                << "\n";
    }
  }
}

std::string_view basename(std::string_view path) {
  auto pos = path.find_last_of("/\\");
  if (pos == std::string_view::npos) {
    return path;
  }
  return path.substr(pos + 1);
}

} // namespace dwarfs
