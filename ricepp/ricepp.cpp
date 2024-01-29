/* vim:set ts=2 sw=2 sts=2 et: */
/**
 * \author     Marcus Holland-Moritz (github@mhxnet.de)
 * \copyright  Copyright (c) Marcus Holland-Moritz
 *
 * This file is part of ricepp.
 *
 * ricepp is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * ricepp is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with ricepp.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <cassert>
#include <cstdint>

#include <ricepp/bitstream_reader.h>
#include <ricepp/bitstream_writer.h>
#include <ricepp/byteswap.h>
#include <ricepp/codec.h>
#include <ricepp/ricepp.h>

namespace ricepp {

namespace {

template <std::unsigned_integral ValueType>
class dynamic_pixel_traits {
 public:
  using value_type = ValueType;
  static constexpr size_t const kBitCount =
      std::numeric_limits<value_type>::digits;
  static constexpr value_type const kAllOnes =
      std::numeric_limits<value_type>::max();

  dynamic_pixel_traits(std::endian byteorder,
                       unsigned unused_lsb_count) noexcept
      : unused_lsb_count_{unused_lsb_count}
      , byteorder_{byteorder}
#ifndef NDEBUG
      , lsb_mask_{static_cast<value_type>(~(kAllOnes << unused_lsb_count))}
      , msb_mask_{static_cast<value_type>(~(kAllOnes >> unused_lsb_count))}
#endif
  {
    assert(unused_lsb_count < kBitCount);
  }

  [[nodiscard]] value_type read(value_type value) const noexcept {
    value_type tmp = byteswap(value, byteorder_);
    assert((tmp & lsb_mask_) == 0);
    return tmp >> unused_lsb_count_;
  }

  [[nodiscard]] value_type write(value_type value) const noexcept {
    assert((value & msb_mask_) == 0);
    return byteswap(static_cast<value_type>(value << unused_lsb_count_),
                    byteorder_);
  }

 private:
  unsigned const unused_lsb_count_;
  std::endian const byteorder_;
#ifndef NDEBUG
  value_type const lsb_mask_;
  value_type const msb_mask_;
#endif
};

template <size_t MaxBlockSize, size_t ComponentStreamCount,
          typename PixelTraits>
class codec_impl final
    : public codec_interface<typename PixelTraits::value_type>,
      public PixelTraits {
 public:
  using pixel_type = typename PixelTraits::value_type;
  using codec_type = codec<MaxBlockSize, ComponentStreamCount, PixelTraits>;

  codec_impl(PixelTraits const& traits, size_t block_size)
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

  void decode(std::span<pixel_type> output,
              std::span<uint8_t const> input) const override {
    decode_impl(output.data(), output.size(), input.data(), input.size());
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

  void decode_impl(pixel_type* __restrict output, size_t output_size,
                   uint8_t const* __restrict input, size_t input_size) const {
    return decode_impl(std::span<pixel_type>{output, output_size},
                       std::span<uint8_t const>{input, input_size});
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

  void decode_impl(std::span<pixel_type> output,
                   std::span<uint8_t const> input) const {
    bitstream_reader reader{input.begin(), input.end()};
    codec_type codec{block_size_, *this};
    codec.decode(output, reader);
  }

 private:
  size_t const block_size_;
};

template <size_t ComponentStreamCount, typename PixelTraits>
std::unique_ptr<codec_interface<typename PixelTraits::value_type>>
create_codec_(size_t block_size, PixelTraits const& traits) {
  if (block_size <= 512) {
    return std::make_unique<codec_impl<512, ComponentStreamCount, PixelTraits>>(
        traits, block_size);
  }

  return nullptr;
}

template <typename PixelTraits>
std::unique_ptr<codec_interface<typename PixelTraits::value_type>>
create_codec_(size_t block_size, size_t component_stream_count,
              PixelTraits const& traits) {
  switch (component_stream_count) {
  case 1:
    return create_codec_<1, PixelTraits>(block_size, traits);

  case 2:
    return create_codec_<2, PixelTraits>(block_size, traits);

  default:
    break;
  }

  return nullptr;
}

} // namespace

template <>
std::unique_ptr<codec_interface<uint16_t>>
create_codec<uint16_t>(codec_config const& config) {
  using pixel_traits = dynamic_pixel_traits<uint16_t>;

  if (auto codec = create_codec_<pixel_traits>(
          config.block_size, config.component_stream_count,
          pixel_traits{config.byteorder, config.unused_lsb_count})) {
    return codec;
  }

  throw std::runtime_error("Unsupported configuration");
}

} // namespace ricepp
