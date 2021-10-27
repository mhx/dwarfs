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

#include <array>
#include <charconv>
#include <climits>
#include <string>
#include <string_view>

#include <unistd.h>

#include <folly/String.h>

#include "dwarfs/error.h"
#include "dwarfs/util.h"

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
  return trimmed(folly::prettyPrint(sec, folly::PRETTY_TIME, false));
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

std::string get_program_path() {
  static const std::array<const char*, 3> paths = {{
      "/proc/self/exe",
      "/proc/curproc/file",
      "/proc/self/path/a.out",
  }};

  for (auto cand : paths) {
    std::array<char, PATH_MAX> linkname;

    auto r = ::readlink(cand, linkname.data(), PATH_MAX);

    if (r == -1) {
      continue;
    }

    return std::string(linkname.data(), r);
  }

  return std::string();
}

} // namespace dwarfs
