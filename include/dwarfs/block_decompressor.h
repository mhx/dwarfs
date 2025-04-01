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
#include <cstdio>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>

#include <dwarfs/byte_buffer.h>
#include <dwarfs/byte_buffer_factory.h>
#include <dwarfs/compression.h>

namespace dwarfs {

class block_decompressor {
 public:
  block_decompressor(compression_type type, std::span<uint8_t const> data);

  shared_byte_buffer start_decompression(mutable_byte_buffer target);
  shared_byte_buffer start_decompression(byte_buffer_factory const& bbf);

  bool decompress_frame(size_t frame_size = BUFSIZ) {
    return impl_->decompress_frame(frame_size);
  }

  size_t uncompressed_size() const { return impl_->uncompressed_size(); }

  compression_type type() const { return impl_->type(); }

  std::optional<std::string> metadata() const { return impl_->metadata(); }

  static shared_byte_buffer
  decompress(compression_type type, std::span<uint8_t const> data);

  class impl {
   public:
    virtual ~impl() = default;

    virtual void start_decompression(mutable_byte_buffer target) = 0;
    virtual bool decompress_frame(size_t frame_size) = 0;
    virtual size_t uncompressed_size() const = 0;
    virtual std::optional<std::string> metadata() const = 0;

    virtual compression_type type() const = 0;
  };

 private:
  std::unique_ptr<impl> impl_;
};

} // namespace dwarfs
