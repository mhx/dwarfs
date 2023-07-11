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

#include <cassert>

#include <brotli/decode.h>
#include <brotli/encode.h>

#include <fmt/format.h>

#include <folly/Varint.h>

#include "dwarfs/block_compressor.h"
#include "dwarfs/error.h"
#include "dwarfs/fstypes.h"
#include "dwarfs/option_map.h"

namespace dwarfs {

namespace {

class brotli_block_compressor final : public block_compressor::impl {
 public:
  brotli_block_compressor(uint32_t quality, uint32_t window_bits)
      : quality_{quality}
      , window_bits_{window_bits} {}

  brotli_block_compressor(const brotli_block_compressor& rhs) = default;

  std::unique_ptr<block_compressor::impl> clone() const override {
    return std::make_unique<brotli_block_compressor>(*this);
  }

  std::vector<uint8_t>
  compress(const std::vector<uint8_t>& data) const override {
    std::vector<uint8_t> compressed;
    compressed.resize(folly::kMaxVarintLength64 +
                      ::BrotliEncoderMaxCompressedSize(data.size()));
    size_t size_size = folly::encodeVarint(data.size(), compressed.data());
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
    return compressed;
  }

  std::vector<uint8_t> compress(std::vector<uint8_t>&& data) const override {
    return compress(data);
  }

  compression_type type() const override { return compression_type::BROTLI; }

 private:
  uint32_t const quality_;
  uint32_t const window_bits_;
};

class brotli_block_decompressor final : public block_decompressor::impl {
 public:
  brotli_block_decompressor(const uint8_t* data, size_t size,
                            std::vector<uint8_t>& target)
      : brotli_block_decompressor(folly::Range<uint8_t const*>(data, size),
                                  target) {}

  brotli_block_decompressor(folly::Range<uint8_t const*> data,
                            std::vector<uint8_t>& target)
      : decompressed_{target}
      , uncompressed_size_{folly::decodeVarint(data)}
      , data_{data.data()}
      , size_{data.size()}
      , decoder_{::BrotliDecoderCreateInstance(nullptr, nullptr, nullptr),
                 &::BrotliDecoderDestroyInstance} {
    if (!decoder_) {
      DWARFS_THROW(runtime_error, "could not create brotli decoder");
    }
    if (!::BrotliDecoderSetParameter(decoder_.get(),
                                     BROTLI_DECODER_PARAM_LARGE_WINDOW, 1)) {
      DWARFS_THROW(runtime_error, "could not set brotli decoder paramter");
    }
    try {
      decompressed_.reserve(uncompressed_size_);
    } catch (std::bad_alloc const&) {
      DWARFS_THROW(
          runtime_error,
          fmt::format("could not reserve {} bytes for decompressed block",
                      uncompressed_size_));
    }
  }

  compression_type type() const override { return compression_type::BROTLI; }

  bool decompress_frame(size_t frame_size) override {
    size_t pos = decompressed_.size();

    if (pos + frame_size > uncompressed_size_) {
      assert(uncompressed_size_ >= pos);
      frame_size = uncompressed_size_ - pos;
    }

    assert(pos + frame_size <= uncompressed_size_);
    assert(frame_size > 0);

    decompressed_.resize(pos + frame_size);
    uint8_t* next_out = &decompressed_[pos];

    auto res = ::BrotliDecoderDecompressStream(decoder_.get(), &size_, &data_,
                                               &frame_size, &next_out, nullptr);

    if (res == BROTLI_DECODER_RESULT_ERROR) {
      DWARFS_THROW(runtime_error,
                   fmt::format("brotli errro: {}", brotli_error()));
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

  std::vector<uint8_t>& decompressed_;
  const size_t uncompressed_size_;
  uint8_t const* data_;
  size_t size_;
  std::unique_ptr<BrotliDecoderState, decltype(BrotliDecoderDestroyInstance)*>
      decoder_;
};

class brotli_compression_factory : public compression_factory {
 public:
  brotli_compression_factory()
      : options_{
            fmt::format("quality=[{}..{}]", BROTLI_MIN_QUALITY,
                        BROTLI_MAX_QUALITY),
            fmt::format("lgwin=[{}..{}]", BROTLI_MIN_WINDOW_BITS, 30),
        } {}

  std::string_view name() const override { return "brotli"; }

  std::string_view description() const override { return "Brotli compression"; }

  std::vector<std::string> const& options() const override { return options_; }

  std::unique_ptr<block_compressor::impl>
  make_compressor(option_map& om) const override {
    return std::make_unique<brotli_block_compressor>(
        om.get<uint32_t>("quality", BROTLI_DEFAULT_QUALITY),
        om.get<uint32_t>("lgwin", BROTLI_DEFAULT_WINDOW));
  }

  std::unique_ptr<block_decompressor::impl>
  make_decompressor(std::span<uint8_t const> data,
                    std::vector<uint8_t>& target) const override {
    return std::make_unique<brotli_block_decompressor>(data.data(), data.size(),
                                                       target);
  }

 private:
  std::vector<std::string> const options_;
};

} // namespace

REGISTER_COMPRESSION_FACTORY(compression_type::BROTLI,
                             brotli_compression_factory)

} // namespace dwarfs
