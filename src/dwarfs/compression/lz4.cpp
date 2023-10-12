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

#include <lz4.h>
#include <lz4hc.h>

#include "dwarfs/block_compressor.h"
#include "dwarfs/error.h"
#include "dwarfs/fstypes.h"
#include "dwarfs/option_map.h"

namespace dwarfs {

namespace {

struct lz4_compression_policy {
  static size_t compress(const void* src, void* dest, size_t size,
                         size_t destsize, int /*level*/) {
    return folly::to<size_t>(LZ4_compress_default(
        reinterpret_cast<const char*>(src), reinterpret_cast<char*>(dest),
        folly::to<int>(size), folly::to<int>(destsize)));
  }
};

struct lz4hc_compression_policy {
  static size_t compress(const void* src, void* dest, size_t size,
                         size_t destsize, int level) {
    return folly::to<size_t>(LZ4_compress_HC(
        reinterpret_cast<const char*>(src), reinterpret_cast<char*>(dest),
        folly::to<int>(size), folly::to<int>(destsize), level));
  }
};

template <typename Policy>
class lz4_block_compressor final : public block_compressor::impl {
 public:
  explicit lz4_block_compressor(int level = 0)
      : level_(level) {}
  lz4_block_compressor(const lz4_block_compressor& rhs) = default;

  std::unique_ptr<block_compressor::impl> clone() const override {
    return std::make_unique<lz4_block_compressor>(*this);
  }

  std::vector<uint8_t>
  compress(const std::vector<uint8_t>& data) const override {
    std::vector<uint8_t> compressed(
        sizeof(uint32_t) + LZ4_compressBound(folly::to<int>(data.size())));
    *reinterpret_cast<uint32_t*>(&compressed[0]) = data.size();
    auto csize =
        Policy::compress(&data[0], &compressed[sizeof(uint32_t)], data.size(),
                         compressed.size() - sizeof(uint32_t), level_);
    if (csize == 0) {
      DWARFS_THROW(runtime_error, "error during compression");
    }
    if (sizeof(uint32_t) + csize >= data.size()) {
      throw bad_compression_ratio_error();
    }
    compressed.resize(sizeof(uint32_t) + csize);
    return compressed;
  }

  std::vector<uint8_t> compress(std::vector<uint8_t>&& data) const override {
    return compress(data);
  }

  compression_type type() const override { return compression_type::LZ4; }

 private:
  const int level_;
};

class lz4_block_decompressor final : public block_decompressor::impl {
 public:
  lz4_block_decompressor(const uint8_t* data, size_t size,
                         std::vector<uint8_t>& target)
      : decompressed_(target)
      , data_(data + sizeof(uint32_t))
      , input_size_(size - sizeof(uint32_t))
      , uncompressed_size_(get_uncompressed_size(data)) {
    try {
      decompressed_.reserve(uncompressed_size_);
    } catch (std::bad_alloc const&) {
      DWARFS_THROW(
          runtime_error,
          fmt::format("could not reserve {} bytes for decompressed block",
                      uncompressed_size_));
    }
  }

  compression_type type() const override { return compression_type::LZ4; }

  bool decompress_frame(size_t) override {
    if (!error_.empty()) {
      DWARFS_THROW(runtime_error, error_);
    }

    decompressed_.resize(uncompressed_size_);
    auto rv = LZ4_decompress_safe(reinterpret_cast<const char*>(data_),
                                  reinterpret_cast<char*>(&decompressed_[0]),
                                  static_cast<int>(input_size_),
                                  static_cast<int>(uncompressed_size_));

    if (rv < 0) {
      decompressed_.clear();
      error_ = fmt::format("LZ4: decompression failed (error: {})", rv);
      DWARFS_THROW(runtime_error, error_);
    }

    return true;
  }

  size_t uncompressed_size() const override { return uncompressed_size_; }

 private:
  static size_t get_uncompressed_size(const uint8_t* data) {
    uint32_t size;
    ::memcpy(&size, data, sizeof(size));
    return size;
  }

  std::vector<uint8_t>& decompressed_;
  const uint8_t* const data_;
  const size_t input_size_;
  const size_t uncompressed_size_;
  std::string error_;
};

class lz4_compression_factory : public compression_factory {
 public:
  std::string_view name() const override { return "lz4"; }

  std::string_view description() const override { return "LZ4 compression"; }

  std::vector<std::string> const& options() const override { return options_; }

  std::unique_ptr<block_compressor::impl>
  make_compressor(option_map&) const override {
    return std::make_unique<lz4_block_compressor<lz4_compression_policy>>();
  }

  std::unique_ptr<block_decompressor::impl>
  make_decompressor(std::span<uint8_t const> data,
                    std::vector<uint8_t>& target) const override {
    return std::make_unique<lz4_block_decompressor>(data.data(), data.size(),
                                                    target);
  }

 private:
  std::vector<std::string> const options_{};
};

class lz4hc_compression_factory : public compression_factory {
 public:
  lz4hc_compression_factory()
      : options_{fmt::format("level=[{}..{}]", 0, LZ4HC_CLEVEL_MAX)} {}

  std::string_view name() const override { return "lz4hc"; }

  std::string_view description() const override { return "LZ4 HC compression"; }

  std::vector<std::string> const& options() const override { return options_; }

  std::unique_ptr<block_compressor::impl>
  make_compressor(option_map& om) const override {
    return std::make_unique<lz4_block_compressor<lz4hc_compression_policy>>(
        om.get<int>("level", LZ4HC_CLEVEL_DEFAULT));
  }

  std::unique_ptr<block_decompressor::impl>
  make_decompressor(std::span<uint8_t const> data,
                    std::vector<uint8_t>& target) const override {
    return std::make_unique<lz4_block_decompressor>(data.data(), data.size(),
                                                    target);
  }

 private:
  std::vector<std::string> const options_;
};

} // namespace

REGISTER_COMPRESSION_FACTORY(compression_type::LZ4, lz4_compression_factory)
REGISTER_COMPRESSION_FACTORY(compression_type::LZ4HC, lz4hc_compression_factory)

} // namespace dwarfs
