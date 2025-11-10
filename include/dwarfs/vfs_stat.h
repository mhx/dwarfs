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
  uint64_t total_fs_size;
  uint64_t total_allocated_fs_size;
  uint64_t total_hardlink_size;
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
