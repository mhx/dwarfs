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
#include <memory>
#include <optional>
#include <system_error>
#include <vector>

#include <dwarfs/memory_mapping.h>
#include <dwarfs/types.h>

#include <dwarfs/detail/file_extent_info.h>

namespace dwarfs::internal {

class io_ops;

class mappable_file {
 public:
  static readonly_memory_mapping
  map_empty_readonly(io_ops const& ops, size_t size);
  static readonly_memory_mapping
  map_empty_readonly(io_ops const& ops, size_t size, std::error_code& ec);

  static memory_mapping map_empty(io_ops const& ops, size_t size);
  static memory_mapping
  map_empty(io_ops const& ops, size_t size, std::error_code& ec);

  static mappable_file
  create(io_ops const& ops, std::filesystem::path const& path);
  static mappable_file
  create(io_ops const& ops, std::filesystem::path const& path,
         std::error_code& ec);

  mappable_file() = default;

  file_size_t size(std::error_code& ec) const { return impl_->size(&ec); }

  file_size_t size() const { return impl_->size(nullptr); }

  std::vector<dwarfs::detail::file_extent_info>
  get_extents(std::error_code& ec) const {
    return impl_->get_extents(&ec);
  }

  std::vector<dwarfs::detail::file_extent_info> get_extents() const {
    return impl_->get_extents(nullptr);
  }

  std::vector<dwarfs::detail::file_extent_info>
  get_extents_noexcept() const noexcept;

  readonly_memory_mapping map_readonly(std::error_code& ec) const {
    return impl_->map_readonly(std::nullopt, &ec);
  }

  readonly_memory_mapping map_readonly() const {
    return impl_->map_readonly(std::nullopt, nullptr);
  }

  readonly_memory_mapping
  map_readonly(file_off_t offset, size_t size, std::error_code& ec) const {
    return impl_->map_readonly(file_range{offset, size}, &ec);
  }

  readonly_memory_mapping map_readonly(file_off_t offset, size_t size) const {
    return impl_->map_readonly(file_range{offset, size}, nullptr);
  }

  size_t read(std::span<std::byte> buffer, file_off_t offset,
              std::error_code& ec) const {
    return impl_->read(buffer, file_range{offset, buffer.size()}, &ec);
  }

  size_t read(std::span<std::byte> buffer, file_off_t offset) const {
    return impl_->read(buffer, file_range{offset, buffer.size()}, nullptr);
  }

  size_t read(void* buffer, file_off_t offset, size_t size,
              std::error_code& ec) const {
    return impl_->read(std::span{static_cast<std::byte*>(buffer), size},
                       file_range{offset, size}, &ec);
  }

  size_t read(void* buffer, file_off_t offset, size_t size) const {
    return impl_->read(std::span{static_cast<std::byte*>(buffer), size},
                       file_range{offset, size}, nullptr);
  }

  class impl {
   public:
    virtual ~impl() = default;

    virtual file_size_t size(std::error_code* ec) const = 0;
    virtual std::vector<dwarfs::detail::file_extent_info>
    get_extents(std::error_code* ec) const = 0;
    virtual readonly_memory_mapping
    map_readonly(std::optional<file_range> range,
                 std::error_code* ec) const = 0;
    virtual size_t
    read(std::span<std::byte> buffer, std::optional<file_range> range,
         std::error_code* ec) const = 0;
  };

  explicit mappable_file(std::unique_ptr<impl> impl)
      : impl_{std::move(impl)} {}

 private:
  std::unique_ptr<impl> impl_;
};

} // namespace dwarfs::internal
