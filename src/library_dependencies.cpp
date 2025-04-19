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
#include <cassert>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

#include <fmt/format.h>

#include <boost/version.hpp>
#include <openssl/crypto.h>
#include <parallel_hashmap/phmap_config.h>
#include <xxhash.h>

#include <dwarfs/config.h>

#include <dwarfs/library_dependencies.h>

namespace dwarfs {

namespace {

std::string version_to_string(uint64_t version, version_format fmt) {
  switch (fmt) {
  case version_format::maj_min_patch_dec_100:
    return fmt::format("{}.{}.{}", version / 10000, (version / 100) % 100,
                       version % 100);
  case version_format::boost:
    return fmt::format("{}.{}.{}", version / 100000, (version / 100) % 1000,
                       version % 100);
  }

  throw std::invalid_argument("unsupported version format");
}

std::string_view get_crypto_version() {
  std::string_view version_str = ::OpenSSL_version(OPENSSL_VERSION);
  if (version_str.starts_with("OpenSSL ")) {
    version_str.remove_prefix(8);
  } else if (version_str.starts_with("LibreSSL ")) {
    version_str.remove_prefix(9);
  }
  if (auto pos = version_str.find(' '); pos != std::string_view::npos) {
    version_str = version_str.substr(0, pos);
  }
  return version_str;
}

} // namespace

std::string library_dependencies::common_as_string() {
  library_dependencies deps;
  deps.add_common_libraries();
  return deps.as_string();
}

void library_dependencies::add_library(std::string const& name_version_string) {
  auto tmp = name_version_string;
  if (tmp.starts_with("lib")) {
    tmp.erase(0, 3);
  }
  std::ranges::replace(tmp, ' ', '-');
  deps_.insert(tmp);
}

void library_dependencies::add_library(std::string const& library_name,
                                       std::string const& version_string) {
  add_library(fmt::format("{}-{}", library_name, version_string));
}

void library_dependencies::add_library(std::string const& library_name,
                                       uint64_t version, version_format fmt) {
  add_library(library_name, version_to_string(version, fmt));
}

void library_dependencies::add_library(std::string const& library_name,
                                       unsigned major, unsigned minor,
                                       unsigned patch) {
  add_library(library_name, fmt::format("{}.{}.{}", major, minor, patch));
}

void library_dependencies::add_common_libraries() {
  add_library("libxxhash", ::XXH_versionNumber(),
              version_format::maj_min_patch_dec_100);
  add_library("libfmt", FMT_VERSION, version_format::maj_min_patch_dec_100);
  add_library(fmt::format("crypto-{}", get_crypto_version()));
  add_library("libboost", BOOST_VERSION, version_format::boost);
  add_library("phmap", PHMAP_VERSION_MAJOR, PHMAP_VERSION_MINOR,
              PHMAP_VERSION_PATCH);
}

std::string library_dependencies::as_string() const {
  static constexpr size_t width{80};
  static constexpr std::string_view prefix{"using: "};
  std::string rv{prefix};
  size_t col = prefix.size();
  bool first{true};

  for (auto const& dep : deps_) {
    if (col + dep.size() + 2 > width) {
      rv += ",\n";
      rv.append(prefix.size(), ' ');
      col = prefix.size();
    } else if (!first) {
      rv += ", ";
      col += 2;
    }

    rv += dep;
    col += dep.size();
    first = false;
  }

  return rv;
}

} // namespace dwarfs
