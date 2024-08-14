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
#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <iostream>

#if __has_include(<utf8cpp/utf8.h>)
#include <utf8cpp/utf8.h>
#else
#include <utf8.h>
#endif

#include <date/date.h>

#include <folly/ExceptionString.h>
#include <folly/String.h>
#include <folly/portability/Fcntl.h>
#include <folly/portability/SysStat.h>
#include <folly/portability/Windows.h>
#include <folly/system/HardwareConcurrency.h>

#include <dwarfs/config.h>

#ifdef DWARFS_STACKTRACE_ENABLED
#include <folly/debugging/symbolizer/SignalHandler.h>
#endif

#include <dwarfs/conv.h>
#include <dwarfs/error.h>
#include <dwarfs/util.h>

extern "C" int dwarfs_wcwidth(int ucs);

namespace dwarfs {

namespace {

inline std::string trimmed(std::string in) {
  while (!in.empty() && in.back() == ' ') {
    in.pop_back();
  }
  return in;
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

void utf8_sanitize(std::string& str) {
  if (!utf8::is_valid(str)) [[unlikely]] {
    str = utf8::replace_invalid(str);
  }
}

void shorten_path_string(std::string& path, char separator, size_t max_len) {
  if (utf8_display_width(path) > max_len) {
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

    if (utf8_display_width(path) > max_len) {
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
  if (!p.empty()) {
    try {
      p = std::filesystem::canonical(p);
    } catch (std::filesystem::filesystem_error const&) {
      p = std::filesystem::absolute(p);
    }

#ifdef _WIN32
    if (auto wstr = p.wstring(); !wstr.starts_with(L"\\\\")) {
      p = std::filesystem::path(L"\\\\?\\" + wstr);
    }
#endif
  }

  return p;
}

bool getenv_is_enabled(char const* var) {
  if (auto val = std::getenv(var)) {
    if (auto maybeBool = try_to<bool>(val); maybeBool && *maybeBool) {
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

void ensure_binary_mode(std::ostream& os [[maybe_unused]]) {
#ifdef _WIN32
  if (&os == &std::cout) {
    _setmode(_fileno(stdout), _O_BINARY);
  } else if (&os == &std::cerr) {
    _setmode(_fileno(stderr), _O_BINARY);
  }
#endif
}

std::string exception_str(std::exception const& e) {
  return folly::exceptionStr(e).toStdString();
}

std::string exception_str(std::exception_ptr const& e) {
  return folly::exceptionStr(e).toStdString();
}

unsigned int hardware_concurrency() noexcept {
  return folly::hardware_concurrency();
}

int get_current_umask() {
  // I'm pretty certain these warnings by Flawfinder are false positives.
  // After all, we're just doing a no-op by re-setting the original value
  // in order to read it.
  auto mask = ::umask(0077); /* Flawfinder: ignore */
  ::umask(mask);             /* Flawfinder: ignore */
  return mask;
}

void install_signal_handlers() {
#ifdef DWARFS_STACKTRACE_ENABLED
  folly::symbolizer::installFatalSignalHandler();
#endif
}

} // namespace dwarfs
