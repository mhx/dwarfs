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

#include <filesystem>
#include <system_error>

#include <dwarfs/file_extents_iterable.h>
#include <dwarfs/file_segment.h>
#include <dwarfs/io_advice.h>
#include <dwarfs/types.h>

namespace dwarfs::detail {

class file_view_impl {
 public:
  virtual ~file_view_impl() = default;

  virtual file_size_t size() const = 0;

  virtual std::filesystem::path const& path() const = 0;

  virtual file_segment segment_at(file_off_t offset, size_t size) const = 0;

  virtual file_extents_iterable extents() const = 0;

  virtual bool supports_raw_bytes() const noexcept = 0;

  virtual std::span<std::byte const> raw_bytes() const = 0;

  virtual void copy_bytes(void* dest, file_off_t offset, size_t size,
                          std::error_code& ec) const = 0;

  // ----------------------------------------------------------------------
  // TODO: this is mostly all deprecated

  virtual std::error_code release_until(file_off_t offset) const = 0;
};

} // namespace dwarfs::detail
