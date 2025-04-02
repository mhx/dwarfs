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

#include <limits>

#include <dwarfs/reader/block_cache_options.h>
#include <dwarfs/reader/inode_reader_options.h>
#include <dwarfs/reader/metadata_options.h>
#include <dwarfs/reader/mlock_mode.h>
#include <dwarfs/types.h>

namespace dwarfs::reader {

struct filesystem_options {
  static constexpr file_off_t IMAGE_OFFSET_AUTO{-1};

  mlock_mode lock_mode{mlock_mode::NONE};
  file_off_t image_offset{0};
  file_off_t image_size{std::numeric_limits<file_off_t>::max()};
  block_cache_options block_cache{};
  metadata_options metadata{};
  inode_reader_options inode_reader{};
  int inode_offset{0};
};

file_off_t parse_image_offset(std::string const& str);

} // namespace dwarfs::reader
