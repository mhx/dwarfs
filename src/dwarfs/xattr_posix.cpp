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

#include <sys/xattr.h>

#include <folly/String.h>

#include "dwarfs/xattr.h"

namespace dwarfs {

namespace {

ssize_t portable_getxattr(const char* path, const char* name, void* value,
                          size_t size) {
#ifdef __APPLE__
  return ::getxattr(path, name, value, size, 0, 0);
#else
  return ::getxattr(path, name, value, size);
#endif
}

int portable_setxattr(const char* path, const char* name, const void* value,
                      size_t size, int flags) {
#ifdef __APPLE__
  return ::setxattr(path, name, value, size, 0, flags);
#else
  return ::setxattr(path, name, value, size, flags);
#endif
}

int portable_removexattr(const char* path, const char* name) {
#ifdef __APPLE__
  return ::removexattr(path, name, 0);
#else
  return ::removexattr(path, name);
#endif
}

ssize_t portable_listxattr(const char* path, char* list, size_t size) {
#ifdef __APPLE__
  return ::listxattr(path, list, size, 0);
#else
  return ::listxattr(path, list, size);
#endif
}

constexpr size_t kExtraSize{1024};

} // namespace

std::string getxattr(std::filesystem::path const& path, std::string const& name,
                     std::error_code& ec) {
  ec.clear();

  auto cpath = path.c_str();
  auto cname = name.c_str();

  for (;;) {
    ssize_t size = portable_getxattr(cpath, cname, nullptr, 0);

    if (size < 0) {
      break;
    }

    std::string value;
    value.resize(size + kExtraSize);

    size = portable_getxattr(cpath, cname, value.data(), value.size());

    if (size >= 0) {
      value.resize(size);
      return value;
    }

    if (errno != ERANGE) {
      break;
    }
  }

  ec = std::error_code(errno, std::generic_category());

  return {};
}

void setxattr(std::filesystem::path const& path, std::string const& name,
              std::string_view value, std::error_code& ec) {
  ec.clear();

  if (portable_setxattr(path.c_str(), name.c_str(), value.data(), value.size(),
                        0) < 0) {
    ec = std::error_code(errno, std::generic_category());
  }
}

void removexattr(std::filesystem::path const& path, std::string const& name,
                 std::error_code& ec) {
  ec.clear();

  if (portable_removexattr(path.c_str(), name.c_str()) < 0) {
    ec = std::error_code(errno, std::generic_category());
  }
}

std::vector<std::string>
listxattr(std::filesystem::path const& path, std::error_code& ec) {
  ec.clear();

  auto cpath = path.c_str();

  for (;;) {
    ssize_t size = portable_listxattr(cpath, nullptr, 0);

    if (size < 0) {
      break;
    }

    std::string list;
    list.resize(size + kExtraSize);

    size = portable_listxattr(cpath, list.data(), list.size());

    if (size >= 0) {
      std::vector<std::string> names;

      if (size > 0) {
        // drop the last '\0'
        list.resize(size - 1);
        folly::split('\0', list, names);
      }

      return names;
    }

    if (errno != ERANGE) {
      break;
    }
  }

  ec = std::error_code(errno, std::generic_category());

  return {};
}

} // namespace dwarfs
