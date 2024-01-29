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

#include <folly/Varint.h>
#include <folly/json.h>

#include <thrift/lib/cpp2/protocol/Serializer.h>

#include <fmt/format.h>

#include <ricepp/ricepp.h>

#include "dwarfs/block_compressor.h"
#include "dwarfs/compression.h"
#include "dwarfs/error.h"
#include "dwarfs/option_map.h"

#include "dwarfs/gen-cpp2/compression_types.h"

namespace dwarfs {

namespace {

constexpr int RICEPP_VERSION{1};

class ricepp_block_compressor final : public block_compressor::impl {
 public:
  ricepp_block_compressor(size_t block_size)
      : block_size_{block_size} {}

  ricepp_block_compressor(const ricepp_block_compressor& rhs) = default;

  std::unique_ptr<block_compressor::impl> clone() const override {
    return std::make_unique<ricepp_block_compressor>(*this);
  }

  std::vector<uint8_t> compress(const std::vector<uint8_t>& data,
                                std::string const* metadata) const override {
    if (!metadata) {
      DWARFS_THROW(runtime_error,
                   "internal error: ricepp compression requires metadata");
    }

    auto meta = folly::parseJson(*metadata);

    auto endianness = meta["endianness"].asString();
    auto component_count = meta["component_count"].asInt();
    auto unused_lsb_count = meta["unused_lsb_count"].asInt();
    auto bytes_per_sample = meta["bytes_per_sample"].asInt();

    assert(2 <= bytes_per_sample && bytes_per_sample <= 2);
    assert(0 <= unused_lsb_count && unused_lsb_count <= 8);
    assert(1 <= component_count && component_count <= 2);

    if (data.size() % (component_count * bytes_per_sample)) {
      DWARFS_THROW(runtime_error,
                   fmt::format("unexpected data configuration: {} bytes to "
                               "compress, {} components, {} bytes per sample",
                               data.size(), component_count, bytes_per_sample));
    }

    using pixel_type = uint16_t;
    auto byteorder =
        endianness == "big" ? std::endian::big : std::endian::little;

    auto codec = ricepp::create_codec<pixel_type>({
        .block_size = block_size_,
        .component_stream_count = static_cast<size_t>(component_count),
        .byteorder = byteorder,
        .unused_lsb_count = static_cast<unsigned>(unused_lsb_count),
    });

    std::vector<uint8_t> compressed;

    {
      using namespace ::apache::thrift;

      compressed.resize(folly::kMaxVarintLength64);

      size_t pos = 0;
      pos += folly::encodeVarint(data.size(), compressed.data() + pos);
      compressed.resize(pos);

      thrift::compression::ricepp_block_header hdr;
      hdr.block_size() = block_size_;
      hdr.component_count() = component_count;
      hdr.bytes_per_sample() = bytes_per_sample;
      hdr.unused_lsb_count() = unused_lsb_count;
      hdr.big_endian() = byteorder == std::endian::big;
      hdr.ricepp_version() = RICEPP_VERSION;

      std::string hdrbuf;
      CompactSerializer::serialize(hdr, &hdrbuf);

      compressed.resize(pos + hdrbuf.size());
      ::memcpy(&compressed[pos], hdrbuf.data(), hdrbuf.size());
    }

    std::span<pixel_type const> input{
        reinterpret_cast<pixel_type const*>(data.data()),
        data.size() / bytes_per_sample};

    size_t header_size = compressed.size();
    compressed.resize(header_size + codec->worst_case_encoded_bytes(input));

    std::span<uint8_t> buffer(compressed);

    auto output = codec->encode(buffer.subspan(header_size), input);
    compressed.resize(header_size + output.size());
    compressed.shrink_to_fit();

    return compressed;
  }

  std::vector<uint8_t> compress(std::vector<uint8_t>&& data,
                                std::string const* metadata) const override {
    return compress(data, metadata);
  }

  compression_type type() const override { return compression_type::RICEPP; }

  std::string describe() const override {
    return fmt::format("ricepp [block_size={}]", block_size_);
  }

  std::string metadata_requirements() const override {
    folly::dynamic req = folly::dynamic::object
        // clang-format off
      ("endianness",       folly::dynamic::array("set",
                             folly::dynamic::array("big", "little")))
      ("bytes_per_sample", folly::dynamic::array("set",
                             folly::dynamic::array(2)))
      ("component_count",  folly::dynamic::array("range", 1, 2))
      ("unused_lsb_count", folly::dynamic::array("range", 0, 8))
      ; // clang-format on
    return folly::toJson(req);
  }

  compression_constraints
  get_compression_constraints(std::string const& metadata) const override {
    auto meta = folly::parseJson(metadata);

    auto component_count = meta["component_count"].asInt();
    auto bytes_per_sample = meta["bytes_per_sample"].asInt();

    compression_constraints cc;

    cc.granularity = component_count * bytes_per_sample;

    return cc;
  }

 private:
  size_t const block_size_;
};

class ricepp_block_decompressor final : public block_decompressor::impl {
 public:
  ricepp_block_decompressor(const uint8_t* data, size_t size,
                            std::vector<uint8_t>& target)
      : ricepp_block_decompressor(folly::Range<uint8_t const*>(data, size),
                                  target) {}

  ricepp_block_decompressor(folly::Range<uint8_t const*> data,
                            std::vector<uint8_t>& target)
      : decompressed_{target}
      , uncompressed_size_{folly::decodeVarint(data)}
      , header_{decode_header(data)}
      , data_{data.data(), data.size()}
      , codec_{ricepp::create_codec<uint16_t>(
            {.block_size = header_.block_size().value(),
             .component_stream_count = header_.component_count().value(),
             .byteorder = header_.big_endian().value() ? std::endian::big
                                                       : std::endian::little,
             .unused_lsb_count = header_.unused_lsb_count().value()})} {
    if (header_.bytes_per_sample().value() != 2) {
      DWARFS_THROW(runtime_error,
                   fmt::format("[RICEPP] unsupported bytes per sample: {}",
                               header_.bytes_per_sample().value()));
    }

    try {
      decompressed_.reserve(uncompressed_size_);
    } catch (std::bad_alloc const&) {
      DWARFS_THROW(
          runtime_error,
          fmt::format(
              "[RICEPP] could not reserve {} bytes for decompressed block",
              uncompressed_size_));
    }
  }

  compression_type type() const override { return compression_type::RICEPP; }

  std::optional<std::string> metadata() const override {
    folly::dynamic meta = folly::dynamic::object
        // clang-format off
      ("endianness",       header_.big_endian().value() ? "big" : "little")
      ("bytes_per_sample", header_.bytes_per_sample().value())
      ("unused_lsb_count", header_.unused_lsb_count().value())
      ("component_count",  header_.component_count().value())
      ; // clang-format on

    return folly::toJson(meta);
  }

  bool decompress_frame(size_t) override {
    if (!codec_) {
      return false;
    }

    decompressed_.resize(uncompressed_size_);
    std::span<uint16_t> output{
        reinterpret_cast<uint16_t*>(decompressed_.data()),
        decompressed_.size() / 2};

    codec_->decode(output, data_);

    codec_.reset();

    return true;
  }

  size_t uncompressed_size() const override { return uncompressed_size_; }

 private:
  static thrift::compression::ricepp_block_header
  decode_header(folly::Range<uint8_t const*>& range) {
    using namespace ::apache::thrift;
    thrift::compression::ricepp_block_header hdr;
    auto size = CompactSerializer::deserialize(range, hdr);
    range.advance(size);
    if (hdr.ricepp_version().value() > RICEPP_VERSION) {
      DWARFS_THROW(runtime_error,
                   fmt::format("[RICEPP] unsupported version: {}",
                               hdr.ricepp_version().value()));
    }
    return hdr;
  }

  std::vector<uint8_t>& decompressed_;
  size_t const uncompressed_size_;
  thrift::compression::ricepp_block_header const header_;
  std::span<uint8_t const> data_;
  std::unique_ptr<ricepp::codec_interface<uint16_t>> codec_;
};

class ricepp_compression_factory : public compression_factory {
 public:
  ricepp_compression_factory()
      : options_{
            fmt::format("block_size=[{}..{}]", 16, 512),
        } {}

  std::string_view name() const override { return "ricepp"; }

  std::string_view description() const override {
    static std::string const s_desc{"RICEPP compression"};
    return s_desc;
  }

  std::vector<std::string> const& options() const override { return options_; }

  std::unique_ptr<block_compressor::impl>
  make_compressor(option_map& om) const override {
    return std::make_unique<ricepp_block_compressor>(
        om.get<size_t>("block_size", 128));
  }

  std::unique_ptr<block_decompressor::impl>
  make_decompressor(std::span<uint8_t const> data,
                    std::vector<uint8_t>& target) const override {
    return std::make_unique<ricepp_block_decompressor>(data.data(), data.size(),
                                                       target);
  }

 private:
  std::vector<std::string> const options_;
};

} // namespace

REGISTER_COMPRESSION_FACTORY(compression_type::RICEPP,
                             ricepp_compression_factory)

} // namespace dwarfs
