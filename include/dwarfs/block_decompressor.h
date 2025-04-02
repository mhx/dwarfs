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
