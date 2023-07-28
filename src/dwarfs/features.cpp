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
#include <iterator>

#include "dwarfs/features.h"

namespace dwarfs {

namespace {

std::set<std::string> supported_features{
#ifdef DWARFS_HAVE_LIBZSTD
    "zstd",
#endif
#ifdef DWARFS_HAVE_LIBLZ4
    "lz4",
#endif
#ifdef DWARFS_HAVE_LIBLZMA
    "lzma",
#endif
#ifdef DWARFS_HAVE_LIBBROTLI
    "brotli",
#endif
#ifdef DWARFS_HAVE_FLAC
    "flac",
#endif
};

} // namespace

std::set<std::string> get_unsupported_features(std::set<std::string> features) {
  std::set<std::string> rv;
  std::set_difference(features.begin(), features.end(),
                      supported_features.begin(), supported_features.end(),
                      std::inserter(rv, rv.end()));
  return rv;
}

} // namespace dwarfs
