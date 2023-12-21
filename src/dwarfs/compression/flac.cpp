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
#include <cstring>
#include <span>

#include <FLAC++/decoder.h>
#include <FLAC++/encoder.h>

#include <thrift/lib/cpp2/protocol/Serializer.h>

#include <fmt/format.h>

#include <folly/Varint.h>
#include <folly/json.h>

#include "dwarfs/block_compressor.h"
#include "dwarfs/compression.h"
#include "dwarfs/error.h"
#include "dwarfs/option_map.h"
#include "dwarfs/pcm_sample_transformer.h"

#include "dwarfs/gen-cpp2/compression_types.h"

namespace dwarfs {

namespace {

constexpr uint8_t const kFlagBigEndian{0x80};
constexpr uint8_t const kFlagSigned{0x40};
constexpr uint8_t const kFlagLsbPadding{0x20};
constexpr uint8_t const kBytesPerSampleMask{0x03};
constexpr size_t const kBlockSize{65536};

class dwarfs_flac_stream_encoder final : public FLAC::Encoder::Stream {
 public:
  explicit dwarfs_flac_stream_encoder(std::vector<uint8_t>& data)
      : data_{data}
      , pos_{data_.size()} {}

  ::FLAC__StreamEncoderReadStatus
  read_callback(FLAC__byte buffer[], size_t* bytes) override {
    ::memcpy(buffer, data_.data() + pos_, *bytes);
    return FLAC__STREAM_ENCODER_READ_STATUS_CONTINUE;
  }

  ::FLAC__StreamEncoderWriteStatus
  write_callback(const FLAC__byte buffer[], size_t bytes, uint32_t,
                 uint32_t) override {
    size_t end = pos_ + bytes;
    if (data_.size() < end) {
      data_.resize(end);
    }
    ::memcpy(data_.data() + pos_, buffer, bytes);
    pos_ += bytes;
    return FLAC__STREAM_ENCODER_WRITE_STATUS_OK;
  }

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
  std::vector<uint8_t>& data_;
  size_t pos_;
};

class dwarfs_flac_stream_decoder final : public FLAC::Decoder::Stream {
 public:
  dwarfs_flac_stream_decoder(
      std::vector<uint8_t>& target, std::span<uint8_t const> data,
      thrift::compression::flac_block_header const& header)
      : target_{target}
      , data_{data}
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
  write_callback(const ::FLAC__Frame* frame,
                 const FLAC__int32* const buffer[]) override {
    auto samples = frame->header.blocksize;
    auto channels = frame->header.channels;
    tmp_.resize(channels * samples);
    for (uint_fast32_t i = 0; i < samples; ++i) {
      for (uint_fast32_t c = 0; c < channels; ++c) {
        tmp_[i * channels + c] = buffer[c][i];
      }
    }

    auto pos = target_.size();
    size_t size = channels * samples * bytes_per_sample_;

    target_.resize(pos + size);

    xfm_.pack(std::span<uint8_t>(&target_[pos], size), tmp_);

    return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
  }

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
  std::vector<uint8_t>& target_;
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

  flac_block_compressor(const flac_block_compressor& rhs) = default;

  std::unique_ptr<block_compressor::impl> clone() const override {
    return std::make_unique<flac_block_compressor>(*this);
  }

  std::vector<uint8_t> compress(const std::vector<uint8_t>& data,
                                std::string const* metadata) const override {
    if (!metadata) {
      DWARFS_THROW(runtime_error,
                   "internal error: flac compression requires metadata");
    }

    auto meta = folly::parseJson(*metadata);

    auto endianness = meta["endianness"].asString();
    auto signedness = meta["signedness"].asString();
    auto padding = meta["padding"].asString();
    auto num_channels = meta["number_of_channels"].asInt();
    auto bits_per_sample = meta["bits_per_sample"].asInt();
    auto bytes_per_sample = meta["bytes_per_sample"].asInt();

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

    std::vector<uint8_t> compressed;

    {
      using namespace ::apache::thrift;

      compressed.reserve(5 * data.size() / 8); // optimistic guess
      compressed.resize(folly::kMaxVarintLength64);

      size_t pos = 0;
      pos += folly::encodeVarint(data.size(), compressed.data() + pos);
      compressed.resize(pos);

      thrift::compression::flac_block_header hdr;
      hdr.num_channels() = num_channels;
      hdr.bits_per_sample() = bits_per_sample;
      hdr.flags() = flags;

      std::string hdrbuf;
      CompactSerializer::serialize(hdr, &hdrbuf);

      compressed.resize(pos + hdrbuf.size());
      ::memcpy(&compressed[pos], hdrbuf.data(), hdrbuf.size());
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

    const auto samples_per_call = kBlockSize / num_channels;
    std::vector<FLAC__int32> buffer;
    size_t input_pos = 0;

    while (num_samples > 0) {
      size_t n = std::min(num_samples, samples_per_call);
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

    return compressed;
  }

  std::vector<uint8_t> compress(std::vector<uint8_t>&& data,
                                std::string const* metadata) const override {
    return compress(data, metadata);
  }

  compression_type type() const override { return compression_type::FLAC; }

  std::string describe() const override {
    return fmt::format("flac [level={}{}]", level_,
                       exhaustive_ ? ", exhaustive" : "");
  }

  std::string metadata_requirements() const override {
    folly::dynamic req = folly::dynamic::object
        // clang-format off
      ("endianness",         folly::dynamic::array("set",
                               folly::dynamic::array("big", "little")))
      ("signedness",         folly::dynamic::array("set",
                               folly::dynamic::array("signed", "unsigned")))
      ("padding",            folly::dynamic::array("set",
                               folly::dynamic::array("msb", "lsb")))
      ("bytes_per_sample",   folly::dynamic::array("range", 1, 4))
      ("bits_per_sample",    folly::dynamic::array("range", 8, 32))
      ("number_of_channels", folly::dynamic::array("range", 1, 8))
      ; // clang-format on
    return folly::toJson(req);
  }

  compression_constraints
  get_compression_constraints(std::string const& metadata) const override {
    auto meta = folly::parseJson(metadata);

    auto num_channels = meta["number_of_channels"].asInt();
    auto bytes_per_sample = meta["bytes_per_sample"].asInt();

    compression_constraints cc;

    cc.granularity = num_channels * bytes_per_sample;

    return cc;
  }

 private:
  uint32_t const level_;
  bool const exhaustive_;
};

class flac_block_decompressor final : public block_decompressor::impl {
 public:
  flac_block_decompressor(const uint8_t* data, size_t size,
                          std::vector<uint8_t>& target)
      : flac_block_decompressor(folly::Range<uint8_t const*>(data, size),
                                target) {}

  flac_block_decompressor(folly::Range<uint8_t const*> data,
                          std::vector<uint8_t>& target)
      : decompressed_{target}
      , uncompressed_size_{folly::decodeVarint(data)}
      , header_{decode_header(data)}
      , decoder_{std::make_unique<dwarfs_flac_stream_decoder>(
            decompressed_, std::span<uint8_t const>(data.data(), data.size()),
            header_)} {
    decoder_->set_md5_checking(false);
    decoder_->set_metadata_ignore_all();

    if (auto status = decoder_->init();
        status != FLAC__STREAM_DECODER_INIT_STATUS_OK) {
      DWARFS_THROW(runtime_error,
                   fmt::format("[FLAC] could not initialize decoder: {}",
                               FLAC__StreamDecoderInitStatusString[status]));
    }

    try {
      decompressed_.reserve(uncompressed_size_);
    } catch (std::bad_alloc const&) {
      DWARFS_THROW(
          runtime_error,
          fmt::format(
              "[FLAC] could not reserve {} bytes for decompressed block",
              uncompressed_size_));
    }
  }

  compression_type type() const override { return compression_type::FLAC; }

  std::optional<std::string> metadata() const override {
    auto const flags = header_.flags().value();
    folly::dynamic meta = folly::dynamic::object
        // clang-format off
      ("endianness",         flags & kFlagBigEndian ? "big" : "little")
      ("signedness",         flags & kFlagSigned ? "signed" : "unsigned")
      ("padding",            flags & kFlagLsbPadding ? "lsb" : "msb")
      ("bytes_per_sample",   (flags & kBytesPerSampleMask) + 1)
      ("bits_per_sample",    header_.bits_per_sample().value())
      ("number_of_channels", header_.num_channels().value())
      ; // clang-format on

    return folly::toJson(meta);
  }

  bool decompress_frame(size_t frame_size) override {
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
  decode_header(folly::Range<uint8_t const*>& range) {
    using namespace ::apache::thrift;
    thrift::compression::flac_block_header hdr;
    auto size = CompactSerializer::deserialize(range, hdr);
    range.advance(size);
    return hdr;
  }

  std::vector<uint8_t>& decompressed_;
  folly::Range<uint8_t const*> backup_data_;

  size_t const uncompressed_size_;
  thrift::compression::flac_block_header const header_;
  std::unique_ptr<dwarfs_flac_stream_decoder> decoder_;
};

class flac_compression_factory : public compression_factory {
 public:
  flac_compression_factory()
      : options_{
            fmt::format("level=[0..8]"),
            fmt::format("exhaustive"),
        } {}

  std::string_view name() const override { return "flac"; }

  std::string_view description() const override {
    static std::string const s_desc{
        fmt::format("FLAC compression (libFLAC++ {})", ::FLAC__VERSION_STRING)};
    return s_desc;
  }

  std::vector<std::string> const& options() const override { return options_; }

  std::unique_ptr<block_compressor::impl>
  make_compressor(option_map& om) const override {
    return std::make_unique<flac_block_compressor>(
        om.get<uint32_t>("level", 5), om.get<bool>("exhaustive", false));
  }

  std::unique_ptr<block_decompressor::impl>
  make_decompressor(std::span<uint8_t const> data,
                    std::vector<uint8_t>& target) const override {
    return std::make_unique<flac_block_decompressor>(data.data(), data.size(),
                                                     target);
  }

 private:
  std::vector<std::string> const options_;
};

} // namespace

REGISTER_COMPRESSION_FACTORY(compression_type::FLAC, flac_compression_factory)

} // namespace dwarfs
