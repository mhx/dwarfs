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

#include <cstdint>

namespace dwarfs {

struct vfs_stat {
  using blkcnt_type = uint64_t;
  using filcnt_type = uint64_t;

  uint64_t bsize;
  uint64_t frsize;
  blkcnt_type blocks;
  filcnt_type files;
  uint64_t namemax;
  bool readonly;
};

template <typename T>
void copy_vfs_stat(T* out, vfs_stat const& in) {
  out->f_bsize = in.bsize;
  out->f_frsize = in.frsize;
  out->f_blocks = in.blocks;
  out->f_files = in.files;
  out->f_namemax = in.namemax;
}

} // namespace dwarfs
