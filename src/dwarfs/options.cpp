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

#include <ostream>
#include <string>

#include <fmt/format.h>

#include "dwarfs/error.h"
#include "dwarfs/options.h"

namespace dwarfs {

std::ostream& operator<<(std::ostream& os, file_order_mode mode) {
  std::string modestr{"unknown"};

  switch (mode) {
  case file_order_mode::NONE:
    modestr = "none";
    break;
  case file_order_mode::PATH:
    modestr = "path";
    break;
  case file_order_mode::REVPATH:
    modestr = "revpath";
    break;
  case file_order_mode::SIMILARITY:
    modestr = "similarity";
    break;
  case file_order_mode::NILSIMSA:
    modestr = "nilsimsa";
    break;
  }

  return os << modestr;
}

std::ostream& operator<<(std::ostream& os, block_cache_options const& opts) {
  os << fmt::format(
      "max_bytes={}, num_workers={}, decompress_ratio={}, mm_release={}, "
      "init_workers={}, disable_block_integrity_check={}",
      opts.max_bytes, opts.num_workers, opts.decompress_ratio, opts.mm_release,
      opts.init_workers, opts.disable_block_integrity_check);
  return os;
}

mlock_mode parse_mlock_mode(std::string_view mode) {
  if (mode == "none") {
    return mlock_mode::NONE;
  }
  if (mode == "try") {
    return mlock_mode::TRY;
  }
  if (mode == "must") {
    return mlock_mode::MUST;
  }
  DWARFS_THROW(runtime_error, fmt::format("invalid lock mode: {}", mode));
}

} // namespace dwarfs
