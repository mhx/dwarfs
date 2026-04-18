/* vim:set ts=2 sw=2 sts=2 et: */
/**
 * \author     Bertrand Lebonnois
 * \copyright  Copyright (c) Bertrand Lebonnois
 *
 * This file is part of dwarfs.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */

#include <zxc.h>

#include <algorithm>
#include <bit>

#include <fmt/format.h>

#include <dwarfs/compressor_registry.h>
#include <dwarfs/decompressor_registry.h>
#include <dwarfs/error.h>
#include <dwarfs/fstypes.h>
#include <dwarfs/malloc_byte_buffer.h>
#include <dwarfs/option_map.h>
#include <dwarfs/varint.h>

#include "base.h"

namespace dwarfs {

namespace {

class zxc_block_compressor final : public block_compressor::impl {
 public:
  explicit zxc_block_compressor(int level = ::zxc_default_level(),
                                size_t block_size = ZXC_BLOCK_SIZE_DEFAULT)
      : level_{level}
      , block_size_{block_size} {
    zxc_compress_opts_t opts{};
    opts.level = level_;
    opts.block_size = block_size_;
    opts.checksum_enabled = 0;
    cctx_ = ::zxc_create_cctx(&opts);
    if (!cctx_) {
      DWARFS_THROW(runtime_error, "ZXC: failed to create compression context");
    }
  }

  ~zxc_block_compressor() override { ::zxc_free_cctx(cctx_); }

  zxc_block_compressor(zxc_block_compressor const& rhs)
      : level_{rhs.level_}
      , block_size_{rhs.block_size_} {
    zxc_compress_opts_t opts{};
    opts.level = level_;
    opts.block_size = block_size_;
    opts.checksum_enabled = 0;
    cctx_ = ::zxc_create_cctx(&opts);
    if (!cctx_) {
      DWARFS_THROW(runtime_error, "ZXC: failed to create compression context");
    }
  }

  zxc_block_compressor& operator=(zxc_block_compressor const&) = delete;

  std::unique_ptr<block_compressor::impl> clone() const override {
    return std::make_unique<zxc_block_compressor>(*this);
  }

  shared_byte_buffer compress(shared_byte_buffer const& data,
                              std::string const* /*metadata*/) const override {
    auto compressed = malloc_byte_buffer::create();
    compressed.resize(varint::max_size +
                      ::zxc_compress_block_bound(data.size()));

    size_t size_size = varint::encode(data.size(), compressed.data());

    zxc_compress_opts_t copts{};
    copts.level = level_;
    copts.block_size = std::max(
        std::bit_ceil(data.size()),
        static_cast<size_t>(ZXC_BLOCK_SIZE_MIN));
    copts.checksum_enabled = 0;

    auto const csize =
        ::zxc_compress_block(cctx_, data.data(), data.size(),
                             compressed.data() + size_size,
                             compressed.size() - size_size, &copts);

    if (csize < 0) {
      DWARFS_THROW(runtime_error,
                   fmt::format("ZXC: compression failed ({})",
                               ::zxc_error_name(static_cast<int>(csize))));
    }

    compressed.resize(size_size + static_cast<size_t>(csize));

    if (compressed.size() >= data.size()) {
      throw bad_compression_ratio_error();
    }

    compressed.shrink_to_fit();
    return compressed.share();
  }

  compression_type type() const override { return compression_type::ZXC; }

  std::string describe() const override {
    return fmt::format("zxc [level={}, block_size={}]", level_, block_size_);
  }

  std::string metadata_requirements() const override { return {}; }

  compression_constraints
  get_compression_constraints(std::string const&) const override {
    return {};
  }

  size_t estimate_memory_usage(size_t data_size) const override {
    return varint::max_size + ::zxc_compress_block_bound(data_size) + data_size;
  }

 private:
  int const level_;
  size_t const block_size_;
  zxc_cctx* cctx_;
};

class zxc_block_decompressor final : public block_decompressor_base {
 public:
  zxc_block_decompressor(std::span<uint8_t const> data)
      : data_(checked_subspan(data))
      , uncompressed_size_(varint::decode(data_)) {
    if (uncompressed_size_ == 0) {
      DWARFS_THROW(runtime_error,
                   "ZXC: could not determine decompressed size");
    }
    dctx_ = ::zxc_create_dctx();
    if (!dctx_) {
      DWARFS_THROW(runtime_error,
                   "ZXC: failed to create decompression context");
    }
  }

  ~zxc_block_decompressor() override { ::zxc_free_dctx(dctx_); }

  compression_type type() const override { return compression_type::ZXC; }

  bool decompress_frame(size_t /*frame_size*/) override {
    DWARFS_CHECK(decompressed_, "decompression not started");

    if (!error_.empty()) {
      DWARFS_THROW(runtime_error, error_);
    }

    decompressed_.resize(uncompressed_size_);

    zxc_decompress_opts_t opts{};
    opts.checksum_enabled = 0;

    auto const rv =
        ::zxc_decompress_block(dctx_, data_.data(), data_.size(),
                               decompressed_.data(), decompressed_.size(),
                               &opts);

    if (rv < 0) {
      decompressed_.clear();
      error_ = fmt::format("ZXC: decompression failed ({})",
                           ::zxc_error_name(static_cast<int>(rv)));
      DWARFS_THROW(runtime_error, error_);
    }

    return true;
  }

  size_t uncompressed_size() const override { return uncompressed_size_; }

 private:
  static std::span<uint8_t const>
  checked_subspan(std::span<uint8_t const> data) {
    if (data.empty()) {
      DWARFS_THROW(runtime_error, "ZXC: compressed data is empty");
    }
    return data;
  }

  std::span<uint8_t const> data_;
  uint64_t const uncompressed_size_;
  zxc_dctx* dctx_;
  std::string error_;
};

template <typename Base>
class zxc_compression_info : public Base {
 public:
  static constexpr compression_type type{compression_type::ZXC};

  std::string_view name() const override { return "zxc"; }

  std::string_view description() const override {
    static std::string const s_desc{
        fmt::format("ZXC compression (libzxc {})", ::zxc_version_string())};
    return s_desc;
  }

  std::set<std::string> library_dependencies() const override {
    return {fmt::format("libzxc-{}", ::zxc_version_string())};
  }
};

class zxc_compressor_factory final
    : public zxc_compression_info<compressor_factory> {
 public:
  std::span<std::string const> options() const override { return options_; }

  std::unique_ptr<block_compressor::impl>
  create(option_map& om) const override {
    return std::make_unique<zxc_block_compressor>(
        om.get<int>("level", ::zxc_default_level()),
        om.get<size_t>("block_size", ZXC_BLOCK_SIZE_DEFAULT));
  }

 private:
  std::vector<std::string> const options_{
      fmt::format("level=[{}..{}]", ::zxc_min_level(), ::zxc_max_level()),
      fmt::format("block_size=[{}..{}]", ZXC_BLOCK_SIZE_MIN,
                  ZXC_BLOCK_SIZE_MAX)};
};

class zxc_decompressor_factory final
    : public zxc_compression_info<decompressor_factory> {
 public:
  std::unique_ptr<block_decompressor::impl>
  create(std::span<uint8_t const> data) const override {
    return std::make_unique<zxc_block_decompressor>(data);
  }
};

} // namespace

REGISTER_COMPRESSOR_FACTORY(zxc_compressor_factory)
REGISTER_DECOMPRESSOR_FACTORY(zxc_decompressor_factory)

} // namespace dwarfs
