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

#include <any>
#include <cstddef>
#include <filesystem>
#include <system_error>
#include <vector>

#include <dwarfs/detail/file_extent_info.h>
#include <dwarfs/io_advice.h>
#include <dwarfs/types.h>

namespace dwarfs::internal {

enum class memory_access {
  readonly,
  readwrite,
};

class memory_mapping_ops {
 public:
  virtual ~memory_mapping_ops() = default;

  virtual std::any
  open(std::filesystem::path const& path, std::error_code& ec) const = 0;
  virtual void close(std::any const& handle, std::error_code& ec) const = 0;

  virtual file_size_t
  size(std::any const& handle, std::error_code& ec) const = 0;
  virtual size_t granularity() const = 0;

  virtual std::vector<dwarfs::detail::file_extent_info>
  get_extents(std::any const& handle, std::error_code& ec) const = 0;

  virtual size_t pread(std::any const& handle, void* buf, size_t size,
                       file_off_t offset, std::error_code& ec) const = 0;

  virtual void* virtual_alloc(size_t size, memory_access access,
                              std::error_code& ec) const = 0;
  virtual void
  virtual_free(void* addr, size_t size, std::error_code& ec) const = 0;

  virtual void* map(std::any const& handle, file_off_t offset, size_t size,
                    std::error_code& ec) const = 0;
  virtual void unmap(void* addr, size_t size, std::error_code& ec) const = 0;

  virtual void advise(void* addr, size_t size, io_advice advice,
                      std::error_code& ec) const = 0;
  virtual void lock(void* addr, size_t size, std::error_code& ec) const = 0;
};

memory_mapping_ops const& get_native_memory_mapping_ops();

} // namespace dwarfs::internal
