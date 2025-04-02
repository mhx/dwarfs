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

#include <cassert>
#include <cstdint>

#include <iostream>

#include <ricepp/byteswap.h>
#include <ricepp/codec_config.h>
#include <ricepp/detail/compiler.h>

#include "ricepp_cpuspecific.h"

namespace ricepp::detail {

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

  [[nodiscard]] RICEPP_FORCE_INLINE value_type
  read(value_type value) const noexcept {
    value_type tmp = ricepp::byteswap(value, byteorder_);
    assert((tmp & lsb_mask_) == 0);
    return tmp >> unused_lsb_count_;
  }

  [[nodiscard]] RICEPP_FORCE_INLINE value_type
  write(value_type value) const noexcept {
    assert((value & msb_mask_) == 0);
    return ricepp::byteswap(static_cast<value_type>(value << unused_lsb_count_),
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

template <std::unsigned_integral ValueType, std::endian ByteOrder,
          unsigned UnusedLsbCount>
class static_pixel_traits {
 public:
  using value_type = ValueType;
  static constexpr size_t const kBitCount =
      std::numeric_limits<value_type>::digits;
  static constexpr value_type const kAllOnes =
      std::numeric_limits<value_type>::max();
  static constexpr std::endian const kByteOrder = ByteOrder;
  static constexpr unsigned const kUnusedLsbCount = UnusedLsbCount;
  static constexpr value_type const kLsbMask =
      static_cast<value_type>(~(kAllOnes << kUnusedLsbCount));
  static constexpr value_type const kMsbMask =
      static_cast<value_type>(~(kAllOnes >> kUnusedLsbCount));
  static_assert(kUnusedLsbCount < kBitCount);

  [[nodiscard]] static RICEPP_FORCE_INLINE value_type
  read(value_type value) noexcept {
    value_type tmp = ricepp::byteswap<kByteOrder>(value);
    assert((tmp & kLsbMask) == 0);
    return tmp >> kUnusedLsbCount;
  }

  [[nodiscard]] static RICEPP_FORCE_INLINE value_type
  write(value_type value) noexcept {
    assert((value & kMsbMask) == 0);
    return ricepp::byteswap<kByteOrder>(
        static_cast<value_type>(value << kUnusedLsbCount));
  }
};

template <template <std::unsigned_integral> typename CodecInterface,
          template <size_t, size_t, typename> typename CodecImpl,
          size_t ComponentStreamCount, typename PixelTraits>
std::unique_ptr<CodecInterface<typename PixelTraits::value_type>>
create_codec_(size_t block_size, PixelTraits const& traits) {
  if (block_size <= 512) {
    return std::make_unique<CodecImpl<512, ComponentStreamCount, PixelTraits>>(
        traits, block_size);
  }

  return nullptr;
}

template <template <std::unsigned_integral> typename CodecInterface,
          template <size_t, size_t, typename> typename CodecImpl,
          typename PixelTraits>
std::unique_ptr<CodecInterface<typename PixelTraits::value_type>>
create_codec_(size_t block_size, size_t component_stream_count,
              PixelTraits const& traits) {
  switch (component_stream_count) {
  case 1:
    return create_codec_<CodecInterface, CodecImpl, 1, PixelTraits>(block_size,
                                                                    traits);

  case 2:
    return create_codec_<CodecInterface, CodecImpl, 2, PixelTraits>(block_size,
                                                                    traits);

  default:
    break;
  }

  return nullptr;
}

template <template <std::unsigned_integral> typename CodecInterface,
          template <size_t, size_t, typename> typename CodecImpl,
          std::unsigned_integral PixelValueType, std::endian ByteOrder,
          unsigned UnusedLsbCount>
std::unique_ptr<CodecInterface<PixelValueType>>
create_codec_(size_t block_size, size_t component_stream_count) {
  using pixel_traits =
      static_pixel_traits<PixelValueType, ByteOrder, UnusedLsbCount>;

  if (auto codec = create_codec_<CodecInterface, CodecImpl, pixel_traits>(
          block_size, component_stream_count, pixel_traits{})) {
    return codec;
  }

  return nullptr;
}

template <template <std::unsigned_integral> typename CodecInterface,
          template <size_t, size_t, typename> typename CodecImpl,
          std::unsigned_integral PixelValueType>
std::unique_ptr<CodecInterface<PixelValueType>>
create_codec_(codec_config const& config) {
  if (config.byteorder == std::endian::big) {
    switch (config.unused_lsb_count) {
    case 0:
      return create_codec_<CodecInterface, CodecImpl, PixelValueType,
                           std::endian::big, 0>(config.block_size,
                                                config.component_stream_count);

    case 2:
      return create_codec_<CodecInterface, CodecImpl, PixelValueType,
                           std::endian::big, 2>(config.block_size,
                                                config.component_stream_count);

    case 4:
      return create_codec_<CodecInterface, CodecImpl, PixelValueType,
                           std::endian::big, 4>(config.block_size,
                                                config.component_stream_count);
    }
  }

  using pixel_traits = dynamic_pixel_traits<PixelValueType>;

  return create_codec_<CodecInterface, CodecImpl, pixel_traits>(
      config.block_size, config.component_stream_count,
      pixel_traits{config.byteorder, config.unused_lsb_count});
}

} // namespace ricepp::detail
