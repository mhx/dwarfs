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

#include <algorithm>
#include <bit>
#include <memory>
#include <span>
#include <string>

#include <zxc.h>

#include <fmt/format.h>

#include <dwarfs/compressor_factory.h>
#include <dwarfs/decompressor_factory.h>
#include <dwarfs/error_fmt.h>
#include <dwarfs/fstypes.h>
#include <dwarfs/malloc_byte_buffer.h>
#include <dwarfs/option_map.h>

#include "base.h"

namespace dwarfs {

namespace {

constexpr size_t kMinBlockSize = static_cast<size_t>(1) << 12;
constexpr size_t kMaxBlockSize = static_cast<size_t>(1) << 19;

constexpr int kDefaultLevel = ZXC_LEVEL_COMPACT;

constexpr size_t pick_block_size(size_t data_size) {
  return std::clamp<size_t>(std::bit_ceil(std::max<size_t>(data_size, 1)),
                            kMinBlockSize, kMaxBlockSize);
}

class zxc_block_compressor final : public block_compressor::impl {
 public:
  explicit zxc_block_compressor(int level = kDefaultLevel)
      : level_{level} {
    if (level_ < ::zxc_min_level() || level_ > ::zxc_max_level()) {
      DWARFS_THROW(runtime_error,
                   fmt::format("ZXC: invalid compression level {}", level_));
    }
  }

  zxc_block_compressor(zxc_block_compressor const&) = default;

  std::unique_ptr<block_compressor::impl> clone() const override {
    return std::make_unique<zxc_block_compressor>(*this);
  }

  shared_byte_buffer compress(shared_byte_buffer const& data,
                              std::string const* /*metadata*/) const override {
    auto const block_size = pick_block_size(data.size());
    auto const max_size = ::zxc_compress_bound(data.size());
    auto compressed = malloc_byte_buffer::create(max_size);

    std::unique_ptr<::zxc_cctx, decltype(&::zxc_free_cctx)> cctx{
        ::zxc_create_cctx(), &::zxc_free_cctx};

    if (!cctx) {
      DWARFS_THROW(runtime_error, "ZXC: failed to create compression context");
    }

    set_param(cctx.get(), ZXC_C_LEVEL, level_);
    set_param(cctx.get(), ZXC_C_BLOCK_SIZE, static_cast<int64_t>(block_size));

    auto const csize =
        ::zxc_compress_cctx(cctx.get(), data.data(), data.size(),
                            compressed.data(), compressed.size());

    if (csize < 0) {
      DWARFS_THROWF(runtime_error, "ZXC: compression failed ({})",
                    ::zxc_error_name(static_cast<int>(csize)));
    }

    compressed.resize(static_cast<size_t>(csize));

    if (compressed.size() >= data.size()) {
      throw bad_compression_ratio_error();
    }

    compressed.shrink_to_fit();

    return compressed.share();
  }

  compression_type type() const override { return compression_type::ZXC; }

  std::string describe() const override {
    return fmt::format("zxc [level={}]", level_);
  }

  std::string metadata_requirements() const override { return {}; }

  compression_constraints
  get_compression_constraints(std::string const&) const override {
    return {};
  }

  size_t estimate_memory_usage(size_t data_size) const override {
    return ::zxc_compress_bound(data_size) +
           ::zxc_estimate_cctx_size(pick_block_size(data_size), level_) +
           data_size;
  }

 private:
  static void set_param(::zxc_cctx* cctx, zxc_cparam_t param, int64_t value) {
    if (auto const rv = ::zxc_cctx_set_param(cctx, param, value);
        rv != ZXC_OK) {
      DWARFS_THROWF(runtime_error, "ZXC: invalid compression parameter ({})",
                    ::zxc_error_name(rv));
    }
  }

  int const level_;
};

// Decodes a few zxc blocks at a time, so that a partial read of a dwarfs block
// does not have to decode all of it.
class zxc_block_decompressor final : public block_decompressor_base {
 public:
  explicit zxc_block_decompressor(std::span<uint8_t const> data)
      : data_(data)
      , uncompressed_size_(
            ::zxc_get_decompressed_size(data.data(), data.size())) {
    if (data_.empty()) {
      DWARFS_THROW(runtime_error, "ZXC: compressed data is empty");
    }
    if (uncompressed_size_ == 0) {
      DWARFS_THROW(runtime_error, "ZXC: could not determine decompressed size");
    }
  }

  compression_type type() const override { return compression_type::ZXC; }

  bool decompress_frame(size_t frame_size) override {
    DWARFS_CHECK(decompressed_, "decompression not started");

    if (!error_.empty()) {
      DWARFS_THROW(runtime_error, error_);
    }

    auto const pos = decompressed_.size();

    if (pos >= uncompressed_size_) {
      return true;
    }

    auto const remaining = uncompressed_size_ - pos;

    // The decoder uses everything past `pos` as scratch space.
    decompressed_.resize(uncompressed_size_);

    if (!stream_ && frame_size >= remaining) {
      decompress_all();
      return true;
    }

    auto const produced =
        decompress_blocks(pos, std::clamp<size_t>(frame_size, 1, remaining));

    decompressed_.resize(pos + produced);

    return !stream_;
  }

  size_t uncompressed_size() const override { return uncompressed_size_; }

 private:
  static constexpr size_t kInputSliceSize = 64 * 1024;

  void decompress_all() {
    auto const rv = ::zxc_decompress(
        data_.data(), data_.size(), decompressed_.data(), decompressed_.size());

    if (rv < 0) {
      fail(rv, 0);
    }
    if (static_cast<size_t>(rv) != uncompressed_size_) {
      fail(ZXC_ERROR_CORRUPT_DATA, 0);
    }
  }

  // Decodes whole zxc blocks until at least `want` bytes have been produced
  // and returns the number of bytes produced. Releases the stream once the
  // frame is complete.
  size_t decompress_blocks(size_t pos, size_t want) {
    if (!stream_) {
      stream_.reset(::zxc_dstream_create(nullptr));
      if (!stream_) {
        fail(ZXC_ERROR_MEMORY, pos);
      }
    }

    auto const remaining = uncompressed_size_ - pos;
    zxc_outbuf_t out{decompressed_.data() + pos, remaining, 0};

    // The decoder runs as far as its input allows, so the input is fed in
    // slices; a slice can hold several highly compressible blocks. The frame
    // only completes once its footer has been consumed.
    while (out.pos < want || out.pos == remaining) {
      zxc_inbuf_t in{data_.data(), std::min(kInputSliceSize, data_.size()), 0};
      auto const rv = ::zxc_dstream_decompress(stream_.get(), &out, &in);

      if (rv < 0) {
        fail(rv, pos);
      }

      data_ = data_.subspan(in.pos);

      if (::zxc_dstream_finished(stream_.get())) {
        if (out.pos != remaining) {
          fail(ZXC_ERROR_CORRUPT_DATA, pos);
        }
        stream_.reset();
        break;
      }

      if (rv == 0 && in.pos == 0) {
        fail(ZXC_ERROR_SRC_TOO_SMALL, pos);
      }
    }

    return out.pos;
  }

  [[noreturn]] void fail(int64_t code, size_t valid_size) {
    error_ = fmt::format("ZXC: decompression failed ({})",
                         ::zxc_error_name(static_cast<int>(code)));
    decompressed_.resize(valid_size);
    DWARFS_THROW(runtime_error, error_);
  }

  std::span<uint8_t const> data_;
  size_t const uncompressed_size_;
  std::unique_ptr<::zxc_dstream, decltype(&::zxc_dstream_free)> stream_{
      nullptr, &::zxc_dstream_free};
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

  bool is_experimental() const override { return true; }

  std::unique_ptr<block_compressor::impl>
  create(option_map& om) const override {
    return std::make_unique<zxc_block_compressor>(
        om.get<int>("level", kDefaultLevel));
  }

 private:
  std::vector<std::string> const options_{
      fmt::format("level=[{}..{}]", ::zxc_min_level(), ::zxc_max_level())};
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
