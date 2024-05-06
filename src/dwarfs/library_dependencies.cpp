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

#include <fmt/format.h>

#include <boost/version.hpp>
#include <openssl/crypto.h>
#include <parallel_hashmap/phmap_config.h>
#include <xxhash.h>

#ifdef DWARFS_USE_JEMALLOC
#include <jemalloc/jemalloc.h>
#endif

#include "dwarfs/block_compressor.h"
#include "dwarfs/library_dependencies.h"

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

#ifdef DWARFS_USE_JEMALLOC
std::string get_jemalloc_version() {
  char const* j;
#ifdef __APPLE__
  j = JEMALLOC_VERSION;
#else
  size_t s = sizeof(j);
  ::mallctl("version", &j, &s, nullptr, 0);
#endif
  std::string rv{j};
  if (auto pos = rv.find('-'); pos != std::string::npos) {
    rv.erase(pos, std::string::npos);
  }
  return rv;
}
#endif

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
  std::replace(tmp.begin(), tmp.end(), ' ', '-');
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
  add_library("libcrypto", OPENSSL_version_major(), OPENSSL_version_minor(),
              OPENSSL_version_patch());
  add_library("libboost", BOOST_VERSION, version_format::boost);

#ifdef DWARFS_USE_JEMALLOC
  add_library("libjemalloc", get_jemalloc_version());
#endif

  add_library("phmap", PHMAP_VERSION_MAJOR, PHMAP_VERSION_MINOR,
              PHMAP_VERSION_PATCH);

  compression_registry::instance().for_each_algorithm(
      [this](compression_type, compression_info const& info) {
        for (auto const& lib : info.library_dependencies()) {
          add_library(lib);
        }
      });
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
