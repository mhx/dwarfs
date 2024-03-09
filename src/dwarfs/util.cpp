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

#ifdef _MSC_VER
#include <utf8cpp/utf8.h>
#else
#include <utf8.h>
#endif

#include <folly/String.h>

#include "dwarfs/error.h"
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
} // namespace

std::string size_with_unit(size_t size) {
  return trimmed(folly::prettyPrint(size, folly::PRETTY_BYTES_IEC, true));
}

std::string time_with_unit(double sec) {
  return trimmed(folly::prettyPrint(sec, folly::PRETTY_TIME_HMS, false));
}

size_t parse_size_with_unit(std::string const& str) {
  size_t end = 0;
  size_t size = std::stoul(str, &end);

  if (str[end] == '\0') {
    return size;
  }

  if (str[end + 1] == '\0') {
    switch (str[end]) {
    case 't':
    case 'T':
      size <<= 10;
      [[fallthrough]];
    case 'g':
    case 'G':
      size <<= 10;
      [[fallthrough]];
    case 'm':
    case 'M':
      size <<= 10;
      [[fallthrough]];
    case 'k':
    case 'K':
      size <<= 10;
      return size;
    default:
      break;
    }
  }

  DWARFS_THROW(runtime_error, "invalid size suffix");
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

  case '\0':
  case 's':
    return std::chrono::seconds(value);

  default:
    break;
  }

  DWARFS_THROW(runtime_error, "unsupported time suffix");
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

void shorten_path_string(std::string& path, char separator, size_t max_len) {
  auto len = utf8_display_width(path);

  if (len > max_len) {
    if (max_len < 3) {
      path.clear();
      return;
    }

    size_t start = 0;
    max_len -= 3;

    while (start != std::string::npos &&
           utf8_display_width(path.data() + start, path.size() - start) >
               max_len) {
      start = path.find(separator, start + 1);
    }

    if (start == std::string::npos) {
      start = max_len - len;
    }

    path.replace(0, start, "...");
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

} // namespace dwarfs
