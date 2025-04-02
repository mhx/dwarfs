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

#include <sys/xattr.h>

#include <dwarfs/string.h>
#include <dwarfs/xattr.h>

namespace dwarfs {

namespace {

ssize_t portable_getxattr(char const* path, char const* name, void* value,
                          size_t size) {
#ifdef __APPLE__
  return ::getxattr(path, name, value, size, 0, 0);
#else
  return ::getxattr(path, name, value, size);
#endif
}

int portable_setxattr(char const* path, char const* name, void const* value,
                      size_t size, int flags) {
#ifdef __APPLE__
  return ::setxattr(path, name, value, size, 0, flags);
#else
  return ::setxattr(path, name, value, size, flags);
#endif
}

int portable_removexattr(char const* path, char const* name) {
#ifdef __APPLE__
  return ::removexattr(path, name, 0);
#else
  return ::removexattr(path, name);
#endif
}

ssize_t portable_listxattr(char const* path, char* list, size_t size) {
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
        split_to(list, '\0', names);
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
