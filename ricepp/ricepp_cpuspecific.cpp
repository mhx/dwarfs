/* vim:set ts=2 sw=2 sts=2 et: */
/**
 * \author     Marcus Holland-Moritz (github@mhxnet.de)
 * \copyright  Copyright (c) Marcus Holland-Moritz
 *
 * This file is part of ricepp.
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

#include <ricepp/bitstream_reader.h>
#include <ricepp/bitstream_writer.h>
#include <ricepp/codec.h>

#include "ricepp_cpuspecific_traits.h"

namespace ricepp::detail {

namespace {

template <size_t MaxBlockSize, size_t ComponentStreamCount,
          typename PixelTraits>
class encoder_impl final
    : public encoder_interface<typename PixelTraits::value_type>,
      public PixelTraits {
 public:
  using pixel_type = typename PixelTraits::value_type;
  using codec_type =
      ricepp::codec<MaxBlockSize, ComponentStreamCount, PixelTraits>;

  encoder_impl(PixelTraits const& traits, size_t block_size)
      : PixelTraits{traits}
      , block_size_{block_size} {}

  std::vector<uint8_t>
  encode(std::span<pixel_type const> input) const override {
    return encode_impl(input.data(), input.size());
  }

  size_t worst_case_encoded_bytes(size_t pixel_count) const override {
    codec_type codec{block_size_, *this};
    return worst_case_encoded_bytes_impl(codec, pixel_count);
  }

  size_t
  worst_case_encoded_bytes(std::span<pixel_type const> input) const override {
    return worst_case_encoded_bytes(input.size());
  }

  std::span<uint8_t> encode(std::span<uint8_t> output,
                            std::span<pixel_type const> input) const override {
    return encode_impl(output.data(), output.size(), input.data(),
                       input.size());
  }

 private:
  size_t worst_case_encoded_bytes_impl(codec_type& codec, size_t size) const {
    return (codec.worst_case_bit_count(size) + 8 - 1) / 8;
  }

  std::vector<uint8_t>
  encode_impl(pixel_type const* __restrict input, size_t size) const {
    return encode_impl(std::span<pixel_type const>{input, size});
  }

  std::span<uint8_t>
  encode_impl(uint8_t* __restrict output, size_t output_size,
              pixel_type const* __restrict input, size_t input_size) const {
    return encode_impl(std::span<uint8_t>{output, output_size},
                       std::span<pixel_type const>{input, input_size});
  }

  std::vector<uint8_t> encode_impl(std::span<pixel_type const> input) const {
    std::vector<uint8_t> output;
    codec_type codec{block_size_, *this};
    output.resize(worst_case_encoded_bytes_impl(codec, input.size()));
    bitstream_writer writer{output.begin()};
    codec.encode(input, writer);
    output.resize(std::distance(output.begin(), writer.iterator()));
    return output;
  }

  std::span<uint8_t> encode_impl(std::span<uint8_t> output,
                                 std::span<pixel_type const> input) const {
    codec_type codec{block_size_, *this};
    assert(output.size() >= worst_case_encoded_bytes_impl(codec, input.size()));
    bitstream_writer writer{output.begin()};
    codec.encode(input, writer);
    return std::span<uint8_t>{output.begin(), writer.iterator()};
  }

 private:
  size_t const block_size_;
};

template <size_t MaxBlockSize, size_t ComponentStreamCount,
          typename PixelTraits>
class decoder_impl final
    : public decoder_interface<typename PixelTraits::value_type>,
      public PixelTraits {
 public:
  using pixel_type = typename PixelTraits::value_type;
  using codec_type = codec<MaxBlockSize, ComponentStreamCount, PixelTraits>;

  decoder_impl(PixelTraits const& traits, size_t block_size)
      : PixelTraits{traits}
      , block_size_{block_size} {}

  void decode(std::span<pixel_type> output,
              std::span<uint8_t const> input) const override {
    decode_impl(output.data(), output.size(), input.data(), input.size());
  }

 private:
  void decode_impl(pixel_type* __restrict output, size_t output_size,
                   uint8_t const* __restrict input, size_t input_size) const {
    return decode_impl(std::span<pixel_type>{output, output_size},
                       std::span<uint8_t const>{input, input_size});
  }

  void decode_impl(std::span<pixel_type> output,
                   std::span<uint8_t const> input) const {
    bitstream_reader reader{input.begin(), input.end()};
    codec_type codec{block_size_, *this};
    codec.decode(output, reader);
  }

 private:
  size_t const block_size_;
};

} // namespace

template <>
std::unique_ptr<encoder_interface<uint16_t>>
encoder_cpuspecific_<uint16_t, cpu_variant::RICEPP_CPU_VARIANT>::create(
    codec_config const& config) {
  if (auto encoder =
          create_codec_<encoder_interface, encoder_impl, uint16_t>(config)) {
    return encoder;
  }

  throw std::runtime_error("Unsupported configuration");
}

template <>
std::unique_ptr<decoder_interface<uint16_t>>
decoder_cpuspecific_<uint16_t, cpu_variant::RICEPP_CPU_VARIANT>::create(
    codec_config const& config) {
  if (auto decoder =
          create_codec_<decoder_interface, decoder_impl, uint16_t>(config)) {
    return decoder;
  }

  throw std::runtime_error("Unsupported configuration");
}

} // namespace ricepp::detail
