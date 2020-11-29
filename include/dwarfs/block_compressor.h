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
#include <string>
#include <utility>
#include <vector>

namespace dwarfs {

enum class compression_type : uint8_t {
  NONE = 0,
  LZMA = 1,
  ZSTD = 2,
  LZ4 = 3,
  LZ4HC = 4,
};

class block_compressor {
 public:
  block_compressor(const std::string& spec, size_t block_size = 0);

  block_compressor(const block_compressor& bc)
      : impl_(bc.impl_->clone()) {}

  block_compressor(block_compressor&& bc) = default;
  block_compressor& operator=(block_compressor&& rhs) = default;

  std::vector<uint8_t> compress(const std::vector<uint8_t>& data) const {
    return impl_->compress(data);
  }

  std::vector<uint8_t> compress(std::vector<uint8_t>&& data) const {
    return impl_->compress(std::move(data));
  }

  void append(const uint8_t* data, size_t size, bool last) {
    impl_->append(data, size, last);
  }

  std::vector<uint8_t> move_data() { return impl_->move_data(); }

  compression_type type() const { return impl_->type(); }

  class impl {
   public:
    virtual ~impl() = default;

    virtual std::unique_ptr<impl> clone() const = 0;

    // TODO: obsolete
    virtual std::vector<uint8_t>
    compress(const std::vector<uint8_t>& data) const = 0;
    virtual std::vector<uint8_t>
    compress(std::vector<uint8_t>&& data) const = 0;

    virtual void append(const uint8_t* data, size_t size, bool last) = 0;
    virtual std::vector<uint8_t> move_data() = 0;

    virtual compression_type type() const = 0;
  };

 private:
  std::unique_ptr<impl> impl_;
};

class block_decompressor {
 public:
  block_decompressor(compression_type type, const uint8_t* data, size_t size,
                     std::vector<uint8_t>& target);

  bool decompress_frame(size_t frame_size = BUFSIZ) {
    return impl_->decompress_frame(frame_size);
  }

  size_t uncompressed_size() const { return impl_->uncompressed_size(); }

  compression_type type() const { return impl_->type(); }

  static std::vector<uint8_t>
  decompress(compression_type type, const uint8_t* data, size_t size) {
    std::vector<uint8_t> target;
    block_decompressor bd(type, data, size, target);
    bd.decompress_frame(bd.uncompressed_size());
    return target;
  }

  class impl {
   public:
    virtual ~impl() = default;

    virtual bool decompress_frame(size_t frame_size) = 0;
    virtual size_t uncompressed_size() const = 0;

    virtual compression_type type() const = 0;
  };

 private:
  std::unique_ptr<impl> impl_;
};
} // namespace dwarfs
