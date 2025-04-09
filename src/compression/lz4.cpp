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

#include <lz4.h>
#include <lz4hc.h>

#include <fmt/format.h>

#include <dwarfs/compressor_registry.h>
#include <dwarfs/conv.h>
#include <dwarfs/decompressor_registry.h>
#include <dwarfs/error.h>
#include <dwarfs/fstypes.h>
#include <dwarfs/malloc_byte_buffer.h>
#include <dwarfs/option_map.h>

#include "base.h"

namespace dwarfs {

namespace {

struct lz4_compression_policy {
  static size_t compress(void const* src, void* dest, size_t size,
                         size_t destsize, int /*level*/) {
    return to<size_t>(LZ4_compress_default(static_cast<char const*>(src),
                                           static_cast<char*>(dest),
                                           to<int>(size), to<int>(destsize)));
  }

  static std::string describe(int /*level*/) { return "lz4"; }
};

struct lz4hc_compression_policy {
  static size_t compress(void const* src, void* dest, size_t size,
                         size_t destsize, int level) {
    return to<size_t>(LZ4_compress_HC(static_cast<char const*>(src),
                                      static_cast<char*>(dest), to<int>(size),
                                      to<int>(destsize), level));
  }

  static std::string describe(int level) {
    return fmt::format("lz4hc [level={}]", level);
  }
};

template <typename Policy>
class lz4_block_compressor final : public block_compressor::impl {
 public:
  explicit lz4_block_compressor(int level = 0)
      : level_(level) {}
  lz4_block_compressor(lz4_block_compressor const& rhs) = default;

  std::unique_ptr<block_compressor::impl> clone() const override {
    return std::make_unique<lz4_block_compressor>(*this);
  }

  shared_byte_buffer compress(shared_byte_buffer const& data,
                              std::string const* /*metadata*/) const override {
    auto compressed = malloc_byte_buffer::create(); // TODO: make configurable
    compressed.resize(sizeof(uint32_t) +
                      LZ4_compressBound(to<int>(data.size())));
    // TODO: this should have been a varint; also, if we ever support
    //       big-endian systems, we'll have to properly convert this
    uint32_t size = data.size();
    std::memcpy(compressed.data(), &size, sizeof(size));
    auto csize = Policy::compress(
        data.data(), compressed.data() + sizeof(uint32_t), data.size(),
        compressed.size() - sizeof(uint32_t), level_);
    if (csize == 0) {
      DWARFS_THROW(runtime_error, "error during compression");
    }
    if (sizeof(uint32_t) + csize >= data.size()) {
      throw bad_compression_ratio_error();
    }
    compressed.resize(sizeof(uint32_t) + csize);
    return compressed.share();
  }

  compression_type type() const override { return compression_type::LZ4; }

  std::string describe() const override { return Policy::describe(level_); }

  std::string metadata_requirements() const override { return {}; }

  compression_constraints
  get_compression_constraints(std::string const&) const override {
    return {};
  }

 private:
  int const level_;
};

class lz4_block_decompressor final : public block_decompressor_base {
 public:
  lz4_block_decompressor(std::span<uint8_t const> data)
      : data_(data.subspan(sizeof(uint32_t)))
      , uncompressed_size_(get_uncompressed_size(data.data())) {}

  compression_type type() const override { return compression_type::LZ4; }

  bool decompress_frame(size_t) override {
    DWARFS_CHECK(decompressed_, "decompression not started");

    if (!error_.empty()) {
      DWARFS_THROW(runtime_error, error_);
    }

    decompressed_.resize(uncompressed_size_);
    auto rv = LZ4_decompress_safe(reinterpret_cast<char const*>(data_.data()),
                                  reinterpret_cast<char*>(decompressed_.data()),
                                  static_cast<int>(data_.size()),
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
  static size_t get_uncompressed_size(uint8_t const* data) {
    // TODO: enforce little-endian
    uint32_t size;
    ::memcpy(&size, data, sizeof(size));
    return size;
  }

  std::span<uint8_t const> data_;
  size_t const uncompressed_size_;
  std::string error_;
};

template <typename Base, compression_type Type>
class lz4_compression_info : public Base {
 public:
  static constexpr auto type{Type};

  std::string_view name() const override {
    if constexpr (type == compression_type::LZ4HC) {
      return "lz4hc";
    } else {
      return "lz4";
    }
  }

  std::string_view description() const override {
    static constexpr std::string_view codec = [] {
      if constexpr (type == compression_type::LZ4HC) {
        return "LZ4 HC";
      } else {
        return "LZ4";
      }
    }();
    static std::string const s_desc{fmt::format("{} compression (liblz4 {})",
                                                codec, ::LZ4_versionString())};
    return s_desc;
  }

  std::set<std::string> library_dependencies() const override {
    return {fmt::format("liblz4-{}", ::LZ4_versionString())};
  }
};

class lz4_compressor_factory final
    : public lz4_compression_info<compressor_factory, compression_type::LZ4> {
 public:
  std::span<std::string const> options() const override { return {}; }

  std::unique_ptr<block_compressor::impl> create(option_map&) const override {
    return std::make_unique<lz4_block_compressor<lz4_compression_policy>>();
  }
};

class lz4_decompressor_factory final
    : public lz4_compression_info<decompressor_factory, compression_type::LZ4> {
 public:
  std::unique_ptr<block_decompressor::impl>
  create(std::span<uint8_t const> data) const override {
    return std::make_unique<lz4_block_decompressor>(data);
  }
};

class lz4hc_compressor_factory final
    : public lz4_compression_info<compressor_factory, compression_type::LZ4HC> {
 public:
  std::span<std::string const> options() const override { return options_; }

  std::unique_ptr<block_compressor::impl>
  create(option_map& om) const override {
    return std::make_unique<lz4_block_compressor<lz4hc_compression_policy>>(
        om.get<int>("level", LZ4HC_CLEVEL_DEFAULT));
  }

 private:
  std::vector<std::string> const options_{
      fmt::format("level=[{}..{}]", 0, LZ4HC_CLEVEL_MAX)};
};

class lz4hc_decompressor_factory final
    : public lz4_compression_info<decompressor_factory,
                                  compression_type::LZ4HC> {
 public:
  std::unique_ptr<block_decompressor::impl>
  create(std::span<uint8_t const> data) const override {
    return std::make_unique<lz4_block_decompressor>(data);
  }
};

} // namespace

REGISTER_COMPRESSOR_FACTORY(lz4_compressor_factory)
REGISTER_DECOMPRESSOR_FACTORY(lz4_decompressor_factory)
REGISTER_COMPRESSOR_FACTORY(lz4hc_compressor_factory)
REGISTER_DECOMPRESSOR_FACTORY(lz4hc_decompressor_factory)

} // namespace dwarfs
