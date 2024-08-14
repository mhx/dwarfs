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

#include <fmt/format.h>

#include <folly/Conv.h>

#include <dwarfs/error.h>
#include <dwarfs/reader/filesystem_options.h>
#include <dwarfs/util.h>

namespace dwarfs::reader {

file_off_t parse_image_offset(std::string const& str) {
  if (str == "auto") {
    return filesystem_options::IMAGE_OFFSET_AUTO;
  }

  auto off = folly::tryTo<file_off_t>(str);

  if (!off) {
    auto ce = folly::makeConversionError(off.error(), str);
    DWARFS_THROW(runtime_error,
                 fmt::format("failed to parse image offset: {} ({})", str,
                             exception_str(ce)));
  }

  if (off.value() < 0) {
    DWARFS_THROW(runtime_error, "image offset must be positive");
  }

  return off.value();
}

} // namespace dwarfs::reader
