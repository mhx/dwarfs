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

#ifdef __FreeBSD__
#include <array>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sys/extattr.h>
#include <sys/types.h>
#else
#include <sys/xattr.h>
#endif

#include <dwarfs/string.h>
#include <dwarfs/xattr.h>

namespace dwarfs {

namespace {

#ifdef __FreeBSD__

// Linux-style flags (used by a lot of portable code).
#ifndef XATTR_CREATE
#define XATTR_CREATE 0x1
#endif
#ifndef XATTR_REPLACE
#define XATTR_REPLACE 0x2
#endif

struct parsed_name {
  int ns;
  std::string_view bare; // view into the original 'name'
};

std::optional<parsed_name> parse_namespace(std::string_view full) {
  using std::string_view;
  constexpr string_view user{"user."};
  constexpr string_view system{"system."};

  if (full.rfind(user, 0) == 0) {
    return parsed_name{EXTATTR_NAMESPACE_USER, full.substr(user.size())};
  } else if (full.rfind(system, 0) == 0) {
    return parsed_name{EXTATTR_NAMESPACE_SYSTEM, full.substr(system.size())};
  } else {
    // Linux also has "trusted." (root-only); FreeBSD has no direct userspace
    // equivalent.
    errno = ENOTSUP;
    return std::nullopt;
  }
}

std::string_view ns_prefix(int ns) {
  switch (ns) {
  case EXTATTR_NAMESPACE_USER:
    return "user.";
  case EXTATTR_NAMESPACE_SYSTEM:
    return "system.";
  default:
    return "";
  }
}

// Probe existence to emulate XATTR_CREATE / XATTR_REPLACE.
std::optional<bool>
exists_file(char const* path, int ns, std::string_view bare) {
  ssize_t r = ::extattr_get_file(path, ns, bare.data(), nullptr, 0);
  if (r >= 0) {
    return true;
  }
  if (errno == ENOATTR) {
    return false;
  }
  return std::nullopt; // some other error
}

// Convert FreeBSD list format ([len][name] ... per namespace)
// to Linux format (NUL-separated names that INCLUDE the namespace prefix).
//
// If 'out' is nullptr or 'outsz' == 0, we just compute the size needed.
std::optional<ssize_t>
convert_list_to_linux(char* out, size_t outsz, char const* in, size_t insz,
                      int ns) {
  std::string_view prefix = ns_prefix(ns);
  size_t written = 0;
  size_t pos = 0;

  while (pos < insz) {
    unsigned char n = static_cast<unsigned char>(in[pos]);
    pos += 1;

    if (pos + n > insz) {
      errno = EIO;
      return std::nullopt;
    }

    size_t need = prefix.size() + static_cast<size_t>(n) + 1; // +NUL
    if (out != nullptr) {
      if (written + need > outsz) {
        errno = ERANGE;
        return std::nullopt;
      }
      std::memcpy(out + written, prefix.data(), prefix.size());
      std::memcpy(out + written + prefix.size(), in + pos, n);
      out[written + prefix.size() + n] = '\0';
    }

    written += need;
    pos += n;
  }

  return static_cast<ssize_t>(written);
}

ssize_t portable_getxattr(char const* path, char const* name, void* value,
                          size_t size) {
  auto parsed = parse_namespace(name);
  if (!parsed.has_value()) {
    return -1;
  }
  ssize_t r =
      ::extattr_get_file(path, parsed->ns, parsed->bare.data(), value, size);
  return r;
}

int portable_setxattr(char const* path, char const* name, void const* value,
                      size_t size, int flags) {
  auto parsed = parse_namespace(name);
  if (!parsed.has_value()) {
    return -1;
  }

  if ((flags & (XATTR_CREATE | XATTR_REPLACE)) != 0) {
    auto ex = exists_file(path, parsed->ns, parsed->bare);
    if (!ex.has_value()) {
      return -1;
    }
    if ((flags & XATTR_CREATE) != 0 && ex.value()) {
      errno = EEXIST;
      return -1;
    }
    if ((flags & XATTR_REPLACE) != 0 && !ex.value()) {
      errno = ENOATTR;
      return -1;
    }
  }

  ssize_t r =
      ::extattr_set_file(path, parsed->ns, parsed->bare.data(), value, size);
  if (r < 0) {
    return -1;
  }
  return 0;
}

int portable_removexattr(char const* path, char const* name) {
  auto parsed = parse_namespace(name);
  if (!parsed.has_value()) {
    return -1;
  }

  return ::extattr_delete_file(path, parsed->ns, parsed->bare.data());
}

ssize_t portable_listxattr(char const* path, char* list, size_t size) {
  std::array<int, 2> namespaces{EXTATTR_NAMESPACE_USER,
                                EXTATTR_NAMESPACE_SYSTEM};

  ssize_t total = 0;

  for (int ns : namespaces) {
    // Query size.
    ssize_t need = ::extattr_list_file(path, ns, nullptr, 0);
    if (need < 0) {
      if (errno == EOPNOTSUPP) {
        continue;
      } else {
        return -1;
      }
    }
    if (need == 0) {
      continue;
    }

    // Fetch raw list.
    std::vector<char> buf(static_cast<size_t>(need));
    ssize_t got = ::extattr_list_file(path, ns, buf.data(), buf.size());
    if (got < 0) {
      return -1;
    }

    // Convert to Linux-style NUL-separated with prefixes and append to output.
    if (auto add = convert_list_to_linux(
            list ? list + total : nullptr,
            list ? (size - static_cast<size_t>(total)) : 0, buf.data(),
            static_cast<size_t>(got), ns);
        add.has_value()) {
      total += add.value();
    } else {
      // errno already set by converter.
      return -1;
    }
  }

  return total;
}

#else

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

#endif

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
