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

#include <zstd.h>

#include <fmt/format.h>

#include <dwarfs/compressor_registry.h>
#include <dwarfs/decompressor_registry.h>
#include <dwarfs/error.h>
#include <dwarfs/fstypes.h>
#include <dwarfs/malloc_byte_buffer.h>
#include <dwarfs/option_map.h>

#include "base.h"

#if ZSTD_VERSION_MAJOR > 1 ||                                                  \
    (ZSTD_VERSION_MAJOR == 1 && ZSTD_VERSION_MINOR >= 4)
#define ZSTD_MIN_LEVEL ZSTD_minCLevel()
#else
#define ZSTD_MIN_LEVEL 1
#endif

namespace dwarfs {

namespace {

#ifndef ZSTD_STATIC_LINKING_ONLY

constexpr size_t kZstdMemUsageGranularity = 16384;

constexpr std::array<uint16_t, 23 * 21> zstd_memory_usage{
    3,     4,    6,    10,    18,    34,    41,    56,    64,    80,    80,
    80,    80,   80,   80,    80,    80,    80,    80,    80,    80,    3,
    4,     5,    8,    14,    12,    19,    34,    36,    36,    36,    36,
    36,    36,   36,   36,    36,    36,    36,    36,    36,    3,     4,
    5,     8,    14,   18,    25,    40,    40,    48,    48,    48,    48,
    48,    48,   48,   48,    48,    48,    48,    48,    3,     4,     6,
    10,    18,   34,   41,    56,    64,    80,    80,    80,    80,    80,
    80,    80,   80,   80,    80,    80,    80,    3,     4,     6,     10,
    14,    34,   65,   96,    80,    160,   160,   160,   160,   160,   160,
    160,   160,  160,  160,   160,   160,   3,     4,     6,     10,    14,
    34,    65,   80,   128,   224,   224,   224,   224,   224,   224,   224,
    224,   224,  224,  224,   224,   3,     4,     6,     10,    14,    34,
    65,    80,   224,  224,   224,   224,   224,   224,   224,   224,   224,
    224,   224,  224,  224,   3,     4,     6,     10,    14,    34,    65,
    80,    224,  416,  416,   416,   416,   416,   416,   416,   416,   416,
    416,   416,  416,  3,     4,     6,     10,    14,    34,    65,    80,
    224,   416,  416,  416,   416,   416,   416,   416,   416,   416,   416,
    416,   416,  3,    5,     7,     12,    18,    34,    65,    80,    224,
    416,   800,  800,  800,   800,   800,   800,   800,   800,   800,   800,
    800,   3,    5,    7,     12,    18,    34,    65,    80,    224,   416,
    800,   1568, 1568, 1568,  1568,  1568,  1568,  1568,  1568,  1568,  1568,
    12,    14,   16,   21,    27,    42,    81,    96,    224,   416,   800,
    1568,  1568, 1568, 1568,  1568,  1568,  1568,  1568,  1568,  1568,  13,
    14,    17,   24,   32,    42,    81,    128,   288,   416,   800,   1568,
    3104,  3104, 3104, 3104,  3104,  3104,  3104,  3104,  3104,  13,    14,
    17,    24,   32,   51,    90,    137,   233,   544,   1056,  2080,  2080,
    2080,  2080, 2080, 2080,  2080,  2080,  2080,  2080,  13,    14,    17,
    24,    36,   61,   110,   177,   273,   544,   1056,  2080,  3104,  3104,
    3104,  3104, 3104, 3104,  3104,  3104,  3104,  13,    14,    17,    24,
    36,    61,   110,  177,   273,   544,   1056,  2080,  4128,  4128,  4128,
    4128,  4128, 4128, 4128,  4128,  4128,  13,    14,    17,    24,    36,
    61,    110,  177,  337,   553,   1065,  2089,  2089,  2089,  2089,  2089,
    2089,  2089, 2089, 2089,  2089,  13,    14,    17,    24,    36,    61,
    110,   177,  337,  553,   1065,  2089,  3113,  3113,  3113,  3113,  3113,
    3113,  3113, 3113, 3113,  13,    14,    17,    24,    36,    61,    110,
    177,   337,  593,  1105,  2129,  3153,  3153,  3153,  3153,  3153,  3153,
    3153,  3153, 3153, 13,    14,    17,    24,    36,    61,    110,   177,
    337,   593,  1105, 2129,  3153,  5201,  5201,  5201,  5201,  5201,  5201,
    5201,  5201, 13,   14,    17,    24,    36,    61,    110,   177,   337,
    593,   1105, 2129, 4177,  6225,  10321, 10321, 10321, 10321, 10321, 10321,
    10321, 13,   14,   17,    24,    36,    61,    110,   177,   337,   593,
    1105,  2129, 4177, 8273,  12369, 20561, 20561, 20561, 20561, 20561, 20561,
    13,    14,   17,   24,    36,    61,    110,   177,   337,   593,   1105,
    2129,  4177, 8273, 16465, 24657, 41041, 41562, 41562, 41562, 41562,
};

#endif

class zstd_block_compressor final : public block_compressor::impl {
 public:
  explicit zstd_block_compressor(int level)
      : level_{level} {}

  zstd_block_compressor(zstd_block_compressor const& rhs) = default;

  std::unique_ptr<block_compressor::impl> clone() const override {
    return std::make_unique<zstd_block_compressor>(*this);
  }

  shared_byte_buffer compress(shared_byte_buffer const& data,
                              std::string const* metadata) const override;

  compression_type type() const override { return compression_type::ZSTD; }

  std::string describe() const override {
    return fmt::format("zstd [level={}]", level_);
  }

  std::string metadata_requirements() const override { return {}; }

  compression_constraints
  get_compression_constraints(std::string const&) const override {
    return {};
  }

  size_t estimate_memory_usage(size_t data_size) const override {
#ifdef ZSTD_STATIC_LINKING_ONLY
    // TODO: check if dictSize == 0 is correct
    auto params = ZSTD_getCParams(level_, data_size, 0);
    return ZSTD_estimateCCtxSize_usingCParams(params) + data_size;
#else
    auto const l = std::clamp<int>(level_, 0, 22);
    auto const lg_data_size =
        std::clamp<int>(static_cast<int>(std::ceil(std::log2(data_size))), 10,
                        30) -
        10;
    auto const index = l * 21 + lg_data_size;
    return kZstdMemUsageGranularity * zstd_memory_usage.at(index) + data_size;
#endif
  }

 private:
  int const level_;
};

shared_byte_buffer
zstd_block_compressor::compress(shared_byte_buffer const& data,
                                std::string const* /*metadata*/) const {
  auto compressed = malloc_byte_buffer::create(); // TODO: make configurable
  compressed.resize(ZSTD_compressBound(data.size()));
  auto size = ZSTD_compress(compressed.data(), compressed.size(), data.data(),
                            data.size(), level_);
  if (ZSTD_isError(size)) {
    DWARFS_THROW(runtime_error,
                 fmt::format("ZSTD: {}", ZSTD_getErrorName(size)));
  }
  if (size >= data.size()) {
    throw bad_compression_ratio_error();
  }
  compressed.resize(size);
  compressed.shrink_to_fit();
  return compressed.share();
}

class zstd_block_decompressor final : public block_decompressor_base {
 public:
  zstd_block_decompressor(std::span<uint8_t const> data)
      : data_(data)
      , uncompressed_size_(ZSTD_getFrameContentSize(data.data(), data.size())) {
    switch (uncompressed_size_) {
    case ZSTD_CONTENTSIZE_UNKNOWN:
      DWARFS_THROW(runtime_error, "ZSTD content size unknown");
      break;

    case ZSTD_CONTENTSIZE_ERROR:
      DWARFS_THROW(runtime_error, "ZSTD content size error");
      break;

    default:
      break;
    }
  }

  compression_type type() const override { return compression_type::ZSTD; }

  bool decompress_frame(size_t /*frame_size*/) override {
    DWARFS_CHECK(decompressed_, "decompression not started");

    if (!error_.empty()) {
      DWARFS_THROW(runtime_error, error_);
    }

    decompressed_.resize(uncompressed_size_);
    auto rv = ZSTD_decompress(decompressed_.data(), decompressed_.size(),
                              data_.data(), data_.size());

    if (ZSTD_isError(rv)) {
      decompressed_.clear();
      error_ = fmt::format("ZSTD: {}", ZSTD_getErrorName(rv));
      DWARFS_THROW(runtime_error, error_);
    }

    return true;
  }

  size_t uncompressed_size() const override { return uncompressed_size_; }

 private:
  std::span<uint8_t const> data_;
  unsigned long long const uncompressed_size_;
  std::string error_;
};

template <typename Base>
class zstd_compression_info : public Base {
 public:
  static constexpr compression_type type{compression_type::ZSTD};

  std::string_view name() const override { return "zstd"; }

  std::string_view description() const override {
    static std::string const s_desc{
        fmt::format("ZSTD compression (libzstd {})", ::ZSTD_versionString())};
    return s_desc;
  }

  std::set<std::string> library_dependencies() const override {
    return {fmt::format("libzstd-{}", ::ZSTD_versionString())};
  }
};

class zstd_compressor_factory final
    : public zstd_compression_info<compressor_factory> {
 public:
  std::span<std::string const> options() const override { return options_; }

  std::unique_ptr<block_compressor::impl>
  create(option_map& om) const override {
    return std::make_unique<zstd_block_compressor>(
        om.get<int>("level", ZSTD_maxCLevel()));
  }

 private:
  std::vector<std::string> const options_{
      fmt::format("level=[{}..{}]", ZSTD_MIN_LEVEL, ZSTD_maxCLevel())};
};

class zstd_decompressor_factory final
    : public zstd_compression_info<decompressor_factory> {
 public:
  std::unique_ptr<block_decompressor::impl>
  create(std::span<uint8_t const> data) const override {
    return std::make_unique<zstd_block_decompressor>(data);
  }
};

} // namespace

REGISTER_COMPRESSOR_FACTORY(zstd_compressor_factory)
REGISTER_DECOMPRESSOR_FACTORY(zstd_decompressor_factory)

} // namespace dwarfs
