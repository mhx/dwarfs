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

#include <brotli/decode.h>
#include <brotli/encode.h>

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

class brotli_block_compressor final : public block_compressor::impl {
 public:
  brotli_block_compressor(uint32_t quality, uint32_t window_bits)
      : quality_{quality}
      , window_bits_{window_bits} {}

  brotli_block_compressor(brotli_block_compressor const& rhs) = default;

  std::unique_ptr<block_compressor::impl> clone() const override {
    return std::make_unique<brotli_block_compressor>(*this);
  }

  shared_byte_buffer compress(shared_byte_buffer const& data,
                              std::string const* /*metadata*/) const override {
    auto compressed = malloc_byte_buffer::create(); // TODO: make configurable
    compressed.resize(varint::max_size +
                      ::BrotliEncoderMaxCompressedSize(data.size()));
    size_t size_size = varint::encode(data.size(), compressed.data());
    size_t compressed_size = compressed.size() - size_size;
    if (!::BrotliEncoderCompress(quality_, window_bits_, BROTLI_DEFAULT_MODE,
                                 data.size(), data.data(), &compressed_size,
                                 compressed.data() + size_size)) {
      DWARFS_THROW(runtime_error, "brotli: error during compression");
    }
    compressed.resize(size_size + compressed_size);
    if (compressed.size() >= data.size()) {
      throw bad_compression_ratio_error();
    }
    compressed.shrink_to_fit();
    return compressed.share();
  }

  compression_type type() const override { return compression_type::BROTLI; }

  std::string describe() const override {
    return fmt::format("brotli [quality={}, lgwin={}]", quality_, window_bits_);
  }

  std::string metadata_requirements() const override { return {}; }

  compression_constraints
  get_compression_constraints(std::string const&) const override {
    return {};
  }

 private:
  uint32_t const quality_;
  uint32_t const window_bits_;
};

class brotli_block_decompressor final : public block_decompressor_base {
 public:
  brotli_block_decompressor(std::span<uint8_t const> data)
      : uncompressed_size_{varint::decode(data)}
      , brotli_data_{data.data()}
      , brotli_size_{data.size()}
      , decoder_{::BrotliDecoderCreateInstance(nullptr, nullptr, nullptr),
                 &::BrotliDecoderDestroyInstance} {
    if (!decoder_) {
      DWARFS_THROW(runtime_error, "could not create brotli decoder");
    }
    if (!::BrotliDecoderSetParameter(decoder_.get(),
                                     BROTLI_DECODER_PARAM_LARGE_WINDOW, 1)) {
      DWARFS_THROW(runtime_error, "could not set brotli decoder parameter");
    }
  }

  compression_type type() const override { return compression_type::BROTLI; }

  bool decompress_frame(size_t frame_size) override {
    DWARFS_CHECK(decompressed_, "decompression not started");

    size_t pos = decompressed_.size();

    if (pos + frame_size > uncompressed_size_) {
      assert(uncompressed_size_ >= pos);
      frame_size = uncompressed_size_ - pos;
    }

    assert(pos + frame_size <= uncompressed_size_);
    assert(frame_size > 0);

    decompressed_.resize(pos + frame_size);
    uint8_t* next_out = decompressed_.data() + pos;

    auto res = ::BrotliDecoderDecompressStream(decoder_.get(), &brotli_size_,
                                               &brotli_data_, &frame_size,
                                               &next_out, nullptr);

    if (res == BROTLI_DECODER_RESULT_ERROR) {
      DWARFS_THROW(runtime_error,
                   fmt::format("brotli error: {}", brotli_error()));
    }

    decompressed_.resize(std::distance(decompressed_.data(), next_out));

    return res == BROTLI_DECODER_RESULT_SUCCESS;
  }

  size_t uncompressed_size() const override { return uncompressed_size_; }

 private:
  char const* brotli_error() const {
    return ::BrotliDecoderErrorString(
        ::BrotliDecoderGetErrorCode(decoder_.get()));
  }

  size_t const uncompressed_size_;
  uint8_t const* brotli_data_;
  size_t brotli_size_;
  std::unique_ptr<BrotliDecoderState, decltype(BrotliDecoderDestroyInstance)*>
      decoder_;
};

template <typename Base>
class brotli_compression_info : public Base {
 public:
  static constexpr auto type{compression_type::BROTLI};

  std::string_view name() const override { return "brotli"; }

  static std::string version_string(uint32_t hex) {
    return fmt::format("{}.{}.{}", hex >> 24, (hex >> 12) & 0xFFF, hex & 0xFFF);
  }
};

class brotli_compressor_factory final
    : public brotli_compression_info<compressor_factory> {
 public:
  std::string_view description() const override {
    static std::string const s_desc{
        fmt::format("Brotli compressor (encoder {})",
                    version_string(::BrotliEncoderVersion()))};
    return s_desc;
  }

  std::span<std::string const> options() const override { return options_; }

  std::set<std::string> library_dependencies() const override {
    return {fmt::format("libbrotlienc-{}",
                        version_string(::BrotliEncoderVersion()))};
  }

  std::unique_ptr<block_compressor::impl>
  create(option_map& om) const override {
    return std::make_unique<brotli_block_compressor>(
        om.get<uint32_t>("quality", BROTLI_DEFAULT_QUALITY),
        om.get<uint32_t>("lgwin", BROTLI_DEFAULT_WINDOW));
  }

 private:
  std::vector<std::string> const options_{
      fmt::format("quality=[{}..{}]", BROTLI_MIN_QUALITY, BROTLI_MAX_QUALITY),
      fmt::format("lgwin=[{}..{}]", BROTLI_MIN_WINDOW_BITS, 30),
  };
};

class brotli_decompressor_factory final
    : public brotli_compression_info<decompressor_factory> {
 public:
  std::string_view description() const override {
    static std::string const s_desc{
        fmt::format("Brotli decompressor (decoder {})",
                    version_string(::BrotliDecoderVersion()))};
    return s_desc;
  }

  std::set<std::string> library_dependencies() const override {
    return {fmt::format("libbrotlidec-{}",
                        version_string(::BrotliDecoderVersion()))};
  }

  std::unique_ptr<block_decompressor::impl>
  create(std::span<uint8_t const> data) const override {
    return std::make_unique<brotli_block_decompressor>(data);
  }
};

} // namespace

REGISTER_COMPRESSOR_FACTORY(brotli_compressor_factory)
REGISTER_DECOMPRESSOR_FACTORY(brotli_decompressor_factory)

} // namespace dwarfs
