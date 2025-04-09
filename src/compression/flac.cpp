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
#include <cstring>
#include <span>

#include <FLAC++/decoder.h>
#include <FLAC++/encoder.h>

#include <thrift/lib/cpp2/protocol/Serializer.h>

#include <fmt/format.h>

#include <nlohmann/json.hpp>

#include <dwarfs/compressor_registry.h>
#include <dwarfs/decompressor_registry.h>
#include <dwarfs/error.h>
#include <dwarfs/malloc_byte_buffer.h>
#include <dwarfs/option_map.h>
#include <dwarfs/pcm_sample_transformer.h>
#include <dwarfs/varint.h>

#include <dwarfs/gen-cpp2/compression_types.h>

#include "base.h"

namespace dwarfs {

namespace {

constexpr uint8_t const kFlagBigEndian{0x80};
constexpr uint8_t const kFlagSigned{0x40};
constexpr uint8_t const kFlagLsbPadding{0x20};
constexpr uint8_t const kBytesPerSampleMask{0x03};
constexpr size_t const kBlockSize{65536};

class dwarfs_flac_stream_encoder final : public FLAC::Encoder::Stream {
 public:
  explicit dwarfs_flac_stream_encoder(mutable_byte_buffer& data)
      : data_{data}
      , pos_{data_.size()} {}

  // NOLINTBEGIN(cppcoreguidelines-avoid-c-arrays)
  ::FLAC__StreamEncoderReadStatus
  read_callback(FLAC__byte buffer[], size_t* bytes) override {
    ::memcpy(buffer, data_.data() + pos_, *bytes);
    return FLAC__STREAM_ENCODER_READ_STATUS_CONTINUE;
  }

  ::FLAC__StreamEncoderWriteStatus
  write_callback(FLAC__byte const buffer[], size_t bytes, uint32_t,
                 uint32_t) override {
    size_t end = pos_ + bytes;
    if (data_.size() < end) {
      data_.resize(end);
    }
    ::memcpy(data_.data() + pos_, buffer, bytes);
    pos_ += bytes;
    return FLAC__STREAM_ENCODER_WRITE_STATUS_OK;
  }
  // NOLINTEND(cppcoreguidelines-avoid-c-arrays)

  ::FLAC__StreamEncoderSeekStatus
  seek_callback(FLAC__uint64 absolute_byte_offset) override {
    pos_ = absolute_byte_offset;
    return FLAC__STREAM_ENCODER_SEEK_STATUS_OK;
  }

  ::FLAC__StreamEncoderTellStatus
  tell_callback(FLAC__uint64* absolute_byte_offset) override {
    *absolute_byte_offset = pos_;
    return FLAC__STREAM_ENCODER_TELL_STATUS_OK;
  }

 private:
  mutable_byte_buffer& data_;
  size_t pos_;
};

class dwarfs_flac_stream_decoder final : public FLAC::Decoder::Stream {
 public:
  dwarfs_flac_stream_decoder(
      std::span<uint8_t const> data,
      thrift::compression::flac_block_header const& header)
      : data_{data}
      , header_{header}
      , bytes_per_sample_{(header_.flags().value() & kBytesPerSampleMask) + 1}
      , xfm_{header_.flags().value() & kFlagBigEndian
                 ? pcm_sample_endianness::Big
                 : pcm_sample_endianness::Little,
             header_.flags().value() & kFlagSigned
                 ? pcm_sample_signedness::Signed
                 : pcm_sample_signedness::Unsigned,
             header_.flags().value() & kFlagLsbPadding
                 ? pcm_sample_padding::Lsb
                 : pcm_sample_padding::Msb,
             bytes_per_sample_, header_.bits_per_sample().value()} {}

  void set_target(mutable_byte_buffer target) {
    DWARFS_CHECK(!target_, "target buffer already set");
    target_ = std::move(target);
  }

  // NOLINTBEGIN(cppcoreguidelines-avoid-c-arrays)
  ::FLAC__StreamDecoderReadStatus
  read_callback(FLAC__byte buffer[], size_t* bytes) override {
    if (pos_ >= data_.size()) {
      return FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;
    }

    if (pos_ + *bytes > data_.size()) {
      *bytes = data_.size() - pos_;
    }

    if (*bytes > 0) {
      ::memcpy(buffer, data_.data() + pos_, *bytes);
    }

    pos_ += *bytes;

    return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
  }

  ::FLAC__StreamDecoderWriteStatus
  write_callback(::FLAC__Frame const* frame,
                 FLAC__int32 const* const buffer[]) override {
    auto samples = frame->header.blocksize;
    auto channels = frame->header.channels;
    tmp_.resize(channels * samples);
    for (uint_fast32_t i = 0; i < samples; ++i) {
      for (uint_fast32_t c = 0; c < channels; ++c) {
        tmp_[i * channels + c] = buffer[c][i];
      }
    }

    DWARFS_CHECK(target_, "target buffer not set");

    auto pos = target_.size();
    size_t size = channels * samples * bytes_per_sample_;

    target_.resize(pos + size);

    xfm_.pack(target_.span().subspan(pos, size), tmp_);

    return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
  }
  // NOLINTEND(cppcoreguidelines-avoid-c-arrays)

  void error_callback(::FLAC__StreamDecoderErrorStatus status) override {
    DWARFS_THROW(runtime_error,
                 fmt::format("[FLAC] decoder error: {}",
                             FLAC__StreamDecoderErrorStatusString[status]));
  }

  ::FLAC__StreamDecoderSeekStatus
  seek_callback(FLAC__uint64 absolute_byte_offset) override {
    if (absolute_byte_offset > data_.size()) {
      return ::FLAC__STREAM_DECODER_SEEK_STATUS_ERROR;
    }
    pos_ = absolute_byte_offset;
    return ::FLAC__STREAM_DECODER_SEEK_STATUS_OK;
  }

  ::FLAC__StreamDecoderTellStatus
  tell_callback(FLAC__uint64* absolute_byte_offset) override {
    *absolute_byte_offset = pos_;
    return ::FLAC__STREAM_DECODER_TELL_STATUS_OK;
  }

  ::FLAC__StreamDecoderLengthStatus
  length_callback(FLAC__uint64* stream_length) override {
    *stream_length = data_.size();
    return ::FLAC__STREAM_DECODER_LENGTH_STATUS_OK;
  }

  bool eof_callback() override { return pos_ >= data_.size(); }

 private:
  mutable_byte_buffer target_;
  std::vector<FLAC__int32> tmp_;
  std::span<uint8_t const> data_;
  thrift::compression::flac_block_header const& header_;
  int const bytes_per_sample_;
  pcm_sample_transformer<FLAC__int32> xfm_;
  size_t pos_{0};
};

class flac_block_compressor final : public block_compressor::impl {
 public:
  flac_block_compressor(uint32_t level, bool exhaustive)
      : level_{level}
      , exhaustive_{exhaustive} {}

  flac_block_compressor(flac_block_compressor const& rhs) = default;

  std::unique_ptr<block_compressor::impl> clone() const override {
    return std::make_unique<flac_block_compressor>(*this);
  }

  shared_byte_buffer compress(shared_byte_buffer const& data,
                              std::string const* metadata) const override {
    if (!metadata) {
      DWARFS_THROW(runtime_error,
                   "internal error: flac compression requires metadata");
    }

    auto meta = nlohmann::json::parse(*metadata);

    auto endianness = meta["endianness"].get<std::string>();
    auto signedness = meta["signedness"].get<std::string>();
    auto padding = meta["padding"].get<std::string>();
    auto num_channels = meta["number_of_channels"].get<int>();
    auto bits_per_sample = meta["bits_per_sample"].get<int>();
    auto bytes_per_sample = meta["bytes_per_sample"].get<int>();

    assert(1 <= bytes_per_sample && bytes_per_sample <= 4);
    assert(8 <= bits_per_sample && bits_per_sample <= 32);
    assert(1 <= num_channels);

    if (data.size() % (num_channels * bytes_per_sample)) {
      DWARFS_THROW(
          runtime_error,
          fmt::format("unexpected PCM waveform configuration: {} bytes to "
                      "compress, {} channels, {} bytes per sample",
                      data.size(), num_channels, bytes_per_sample));
    }

    size_t num_samples = data.size() / (num_channels * bytes_per_sample);

    pcm_sample_endianness pcm_end;
    pcm_sample_signedness pcm_sig;
    pcm_sample_padding pcm_pad;

    uint8_t flags = bytes_per_sample - 1;

    if (endianness == "big") {
      flags |= kFlagBigEndian;
      pcm_end = pcm_sample_endianness::Big;
    } else {
      pcm_end = pcm_sample_endianness::Little;
    }

    if (signedness == "signed") {
      flags |= kFlagSigned;
      pcm_sig = pcm_sample_signedness::Signed;
    } else {
      pcm_sig = pcm_sample_signedness::Unsigned;
    }

    if (padding == "lsb") {
      flags |= kFlagLsbPadding;
      pcm_pad = pcm_sample_padding::Lsb;
    } else {
      pcm_pad = pcm_sample_padding::Msb;
    }

    auto compressed = malloc_byte_buffer::create(); // TODO: make configurable

    {
      using namespace ::apache::thrift;

      compressed.reserve(5 * data.size() / 8); // optimistic guess
      compressed.resize(varint::max_size);

      size_t pos = 0;
      pos += varint::encode(data.size(), compressed.data() + pos);
      compressed.resize(pos);

      thrift::compression::flac_block_header hdr;
      hdr.num_channels() = num_channels;
      hdr.bits_per_sample() = bits_per_sample;
      hdr.flags() = flags;

      std::string hdrbuf;
      CompactSerializer::serialize(hdr, &hdrbuf);

      compressed.append(hdrbuf.data(), hdrbuf.size());
    }

    dwarfs_flac_stream_encoder encoder(compressed);

    encoder.set_streamable_subset(false);
    encoder.set_channels(num_channels);
    encoder.set_bits_per_sample(bits_per_sample);
    encoder.set_sample_rate(48000); // TODO: see if a fixed rate makes sense
    encoder.set_compression_level(level_);
    encoder.set_do_exhaustive_model_search(exhaustive_);
    encoder.set_total_samples_estimate(num_samples);

    if (encoder.init() != FLAC__STREAM_ENCODER_INIT_STATUS_OK) {
      DWARFS_THROW(
          runtime_error,
          fmt::format("[FLAC] init: {}", encoder.get_state().as_cstring()));
    }

    pcm_sample_transformer<FLAC__int32> xfm(pcm_end, pcm_sig, pcm_pad,
                                            bytes_per_sample, bits_per_sample);

    auto const samples_per_call = kBlockSize / num_channels;
    std::vector<FLAC__int32> buffer;
    size_t input_pos = 0;

    while (num_samples > 0) {
      auto n = std::min<size_t>(num_samples, samples_per_call);
      buffer.resize(n * num_channels);
      xfm.unpack(buffer,
                 std::span<uint8_t const>(data.data() + input_pos,
                                          buffer.size() * bytes_per_sample));

      if (!encoder.process_interleaved(buffer.data(), n)) {
        DWARFS_THROW(
            runtime_error,
            fmt::format("[FLAC] failed to process interleaved samples: {}",
                        encoder.get_state().as_cstring()));
      }

      input_pos += buffer.size() * bytes_per_sample;
      num_samples -= n;
    }

    if (!encoder.finish()) {
      DWARFS_THROW(runtime_error, "[FLAC] failed to finish encoder");
    }

    // XXX: don't throw this as we're losing metadata
    // if (compressed.size() >= data.size()) {
    //   throw bad_compression_ratio_error();
    // }

    compressed.shrink_to_fit();

    return compressed.share();
  }

  compression_type type() const override { return compression_type::FLAC; }

  std::string describe() const override {
    return fmt::format("flac [level={}{}]", level_,
                       exhaustive_ ? ", exhaustive" : "");
  }

  std::string metadata_requirements() const override {
    using nlj = nlohmann::json;
    nlohmann::json req = {
        {"endianness", nlj::array({"set", nlj::array({"big", "little"})})},
        {"signedness", nlj::array({"set", nlj::array({"signed", "unsigned"})})},
        {"padding", nlj::array({"set", nlj::array({"msb", "lsb"})})},
        {"bytes_per_sample", nlj::array({"range", 1, 4})},
        {"bits_per_sample", nlj::array({"range", 8, 32})},
        {"number_of_channels", nlj::array({"range", 1, 8})},
    };
    return req.dump();
  }

  compression_constraints
  get_compression_constraints(std::string const& metadata) const override {
    auto meta = nlohmann::json::parse(metadata);

    auto num_channels = meta["number_of_channels"].get<int>();
    auto bytes_per_sample = meta["bytes_per_sample"].get<int>();

    compression_constraints cc;

    cc.granularity = num_channels * bytes_per_sample;

    return cc;
  }

 private:
  uint32_t const level_;
  bool const exhaustive_;
};

class flac_block_decompressor final : public block_decompressor_base {
 public:
  flac_block_decompressor(std::span<uint8_t const> data)
      : uncompressed_size_{varint::decode(data)}
      , header_{decode_header(data)}
      , decoder_{std::make_unique<dwarfs_flac_stream_decoder>(data, header_)} {
    decoder_->set_md5_checking(false);
    decoder_->set_metadata_ignore_all();

    if (auto status = decoder_->init();
        status != FLAC__STREAM_DECODER_INIT_STATUS_OK) {
      DWARFS_THROW(runtime_error,
                   fmt::format("[FLAC] could not initialize decoder: {}",
                               FLAC__StreamDecoderInitStatusString[status]));
    }
  }

  void start_decompression(mutable_byte_buffer target) override {
    block_decompressor_base::start_decompression(std::move(target));
    decoder_->set_target(decompressed_);
  }

  compression_type type() const override { return compression_type::FLAC; }

  std::optional<std::string> metadata() const override {
    auto const flags = header_.flags().value();
    nlohmann::json meta{
        {"endianness", flags & kFlagBigEndian ? "big" : "little"},
        {"signedness", flags & kFlagSigned ? "signed" : "unsigned"},
        {"padding", flags & kFlagLsbPadding ? "lsb" : "msb"},
        {"bytes_per_sample", (flags & kBytesPerSampleMask) + 1},
        {"bits_per_sample", header_.bits_per_sample().value()},
        {"number_of_channels", header_.num_channels().value()},
    };
    return meta.dump();
  }

  bool decompress_frame(size_t frame_size) override {
    DWARFS_CHECK(decompressed_, "decompression not started");

    size_t pos = decompressed_.size();

    if (pos + frame_size > uncompressed_size_) {
      assert(uncompressed_size_ >= pos);
      frame_size = uncompressed_size_ - pos;
    }

    size_t wanted = pos + frame_size;

    assert(wanted <= uncompressed_size_);
    assert(frame_size > 0);

    while (decompressed_.size() < wanted) {
      if (!decoder_->process_single()) {
        DWARFS_THROW(runtime_error,
                     fmt::format("[FLAC] failed to process frame: {}",
                                 decoder_->get_state().as_cstring()));
      }
    }

    if (decompressed_.size() == uncompressed_size_) {
      decoder_.reset();
      return true;
    }

    return false;
  }

  size_t uncompressed_size() const override { return uncompressed_size_; }

 private:
  static thrift::compression::flac_block_header
  decode_header(folly::span<uint8_t const>& span) {
    using namespace ::apache::thrift;
    thrift::compression::flac_block_header hdr;
    auto size = CompactSerializer::deserialize(
        folly::ByteRange{span.data(), span.size()}, hdr);
    span = span.subspan(size);
    return hdr;
  }

  size_t const uncompressed_size_;
  thrift::compression::flac_block_header const header_;
  std::unique_ptr<dwarfs_flac_stream_decoder> decoder_;
};

template <typename Base>
class flac_compression_info : public Base {
 public:
  static constexpr auto type{compression_type::FLAC};

  std::string_view name() const override { return "flac"; }

  std::string_view description() const override {
    static std::string const s_desc{
        fmt::format("FLAC compression (libFLAC++ {})", ::FLAC__VERSION_STRING)};
    return s_desc;
  }

  std::set<std::string> library_dependencies() const override {
    return {fmt::format("libFLAC++-{}", ::FLAC__VERSION_STRING)};
  }
};

class flac_compressor_factory final
    : public flac_compression_info<compressor_factory> {
 public:
  std::span<std::string const> options() const override { return options_; }

  std::unique_ptr<block_compressor::impl>
  create(option_map& om) const override {
    return std::make_unique<flac_block_compressor>(
        om.get<uint32_t>("level", 5), om.get<bool>("exhaustive", false));
  }

 private:
  std::vector<std::string> const options_{
      fmt::format("level=[0..8]"),
      fmt::format("exhaustive"),
  };
};

class flac_decompressor_factory final
    : public flac_compression_info<decompressor_factory> {
 public:
  std::unique_ptr<block_decompressor::impl>
  create(std::span<uint8_t const> data) const override {
    return std::make_unique<flac_block_decompressor>(data);
  }
};

} // namespace

REGISTER_COMPRESSOR_FACTORY(flac_compressor_factory)
REGISTER_DECOMPRESSOR_FACTORY(flac_decompressor_factory)

} // namespace dwarfs
