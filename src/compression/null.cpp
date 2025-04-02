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

#include <cassert>

#include <fmt/format.h>

#include <dwarfs/compressor_registry.h>
#include <dwarfs/decompressor_registry.h>
#include <dwarfs/error.h>
#include <dwarfs/fstypes.h>
#include <dwarfs/option_map.h>

#include "base.h"

namespace dwarfs {

namespace {

class null_block_compressor final : public block_compressor::impl {
 public:
  null_block_compressor() = default;
  null_block_compressor(null_block_compressor const& rhs) = default;

  std::unique_ptr<block_compressor::impl> clone() const override {
    return std::make_unique<null_block_compressor>(*this);
  }

  shared_byte_buffer compress(shared_byte_buffer const& data,
                              std::string const* /*metadata*/) const override {
    return data;
  }

  compression_type type() const override { return compression_type::NONE; }

  std::string describe() const override { return "null"; }

  std::string metadata_requirements() const override { return {}; }

  compression_constraints
  get_compression_constraints(std::string const&) const override {
    return {};
  }
};

class null_block_decompressor final : public block_decompressor_base {
 public:
  null_block_decompressor(std::span<uint8_t const> data)
      : data_(data) {}

  compression_type type() const override { return compression_type::NONE; }

  bool decompress_frame(size_t frame_size) override {
    DWARFS_CHECK(decompressed_, "decompression not started");

    if (decompressed_.size() + frame_size > data_.size()) {
      frame_size = data_.size() - decompressed_.size();
    }

    assert(frame_size > 0);

    size_t offset = decompressed_.size();
    decompressed_.resize(offset + frame_size);

    std::copy(data_.data() + offset, data_.data() + offset + frame_size,
              decompressed_.data() + offset);

    return decompressed_.size() == data_.size();
  }

  size_t uncompressed_size() const override { return data_.size(); }

 private:
  std::span<uint8_t const> data_;
};

template <typename Base>
class null_compression_info : public Base {
 public:
  static constexpr compression_type type{compression_type::NONE};

  std::string_view name() const override { return "null"; }

  std::string_view description() const override {
    return "no compression at all";
  }

  std::set<std::string> library_dependencies() const override { return {}; }
};

class null_compressor_factory final
    : public null_compression_info<compressor_factory> {
 public:
  std::span<std::string const> options() const override { return {}; }

  std::unique_ptr<block_compressor::impl> create(option_map&) const override {
    return std::make_unique<null_block_compressor>();
  }
};

class null_decompressor_factory final
    : public null_compression_info<decompressor_factory> {
 public:
  std::unique_ptr<block_decompressor::impl>
  create(std::span<uint8_t const> data) const override {
    return std::make_unique<null_block_decompressor>(data);
  }
};

} // namespace

REGISTER_COMPRESSOR_FACTORY(null_compressor_factory)
REGISTER_DECOMPRESSOR_FACTORY(null_decompressor_factory)

} // namespace dwarfs
