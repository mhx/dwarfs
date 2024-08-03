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

#pragma once

#ifndef _WIN32
#include <sys/uio.h>
#endif

#include <dwarfs/reader/block_range.h>
#include <dwarfs/small_vector.h>

namespace dwarfs::reader {

#ifdef _WIN32
struct dwarfs_iovec {
  void* iov_base;
  size_t iov_len;
};
#else
using dwarfs_iovec = struct ::iovec;
#endif

struct iovec_read_buf {
  // This covers more than 95% of reads
  static constexpr size_t inline_storage = 16;

  void clear() {
    buf.clear();
    ranges.clear();
  }

  small_vector<dwarfs_iovec, inline_storage> buf;
  small_vector<block_range, inline_storage> ranges;
};

} // namespace dwarfs::reader
