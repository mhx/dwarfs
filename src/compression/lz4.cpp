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

#include <fmt/format.h>

#include <dwarfs/block_compressor.h>
#include <dwarfs/conv.h>
#include <dwarfs/error.h>
#include <dwarfs/fstypes.h>
#include <dwarfs/option_map.h>

namespace dwarfs {

namespace {

struct lz4_compression_policy {
  static size_t compress(const void* src, void* dest, size_t size,
                         size_t destsize, int /*level*/) {
    return to<size_t>(LZ4_compress_default(reinterpret_cast<const char*>(src),
                                           reinterpret_cast<char*>(dest),
                                           to<int>(size), to<int>(destsize)));
  }

  static std::string describe(int /*level*/) { return "lz4"; }
};

struct lz4hc_compression_policy {
  static size_t compress(const void* src, void* dest, size_t size,
                         size_t destsize, int level) {
    return to<size_t>(LZ4_compress_HC(reinterpret_cast<const char*>(src),
                                      reinterpret_cast<char*>(dest),
                                      to<int>(size), to<int>(destsize), level));
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
  lz4_block_compressor(const lz4_block_compressor& rhs) = default;

  std::unique_ptr<block_compressor::impl> clone() const override {
    return std::make_unique<lz4_block_compressor>(*this);
  }

  std::vector<uint8_t>
  compress(const std::vector<uint8_t>& data,
           std::string const* /*metadata*/) const override {
    std::vector<uint8_t> compressed(sizeof(uint32_t) +
                                    LZ4_compressBound(to<int>(data.size())));
    // TODO: this should have been a varint; also, if we ever support
    //       big-endian systems, we'll have to properly convert this
    uint32_t size = data.size();
    std::memcpy(&compressed[0], &size, sizeof(size));
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

  std::vector<uint8_t> compress(std::vector<uint8_t>&& data,
                                std::string const* metadata) const override {
    return compress(data, metadata);
  }

  compression_type type() const override { return compression_type::LZ4; }

  std::string describe() const override { return Policy::describe(level_); }

  std::string metadata_requirements() const override { return std::string(); }

  compression_constraints
  get_compression_constraints(std::string const&) const override {
    return compression_constraints();
  }

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

  std::optional<std::string> metadata() const override { return std::nullopt; }

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
    // TODO: enforce little-endian
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
  static constexpr compression_type type{compression_type::LZ4};

  std::string_view name() const override { return "lz4"; }

  std::string_view description() const override {
    static std::string const s_desc{
        fmt::format("LZ4 compression (liblz4 {})", ::LZ4_versionString())};
    return s_desc;
  }

  std::vector<std::string> const& options() const override { return options_; }

  std::set<std::string> library_dependencies() const override {
    return {fmt::format("liblz4-{}", ::LZ4_versionString())};
  }

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
  static constexpr compression_type type{compression_type::LZ4HC};

  lz4hc_compression_factory()
      : options_{fmt::format("level=[{}..{}]", 0, LZ4HC_CLEVEL_MAX)} {}

  std::string_view name() const override { return "lz4hc"; }

  std::string_view description() const override {
    static std::string const s_desc{
        fmt::format("LZ4 HC compression (liblz4 {})", ::LZ4_versionString())};
    return s_desc;
  }

  std::vector<std::string> const& options() const override { return options_; }

  std::set<std::string> library_dependencies() const override {
    return {fmt::format("liblz4-{}", ::LZ4_versionString())};
  }

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

REGISTER_COMPRESSION_FACTORY(lz4_compression_factory)
REGISTER_COMPRESSION_FACTORY(lz4hc_compression_factory)

} // namespace dwarfs
