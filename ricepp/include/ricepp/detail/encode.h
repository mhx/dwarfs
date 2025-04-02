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

#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <concepts>
#include <type_traits>

#include <range/v3/range/concepts.hpp>

#include <ricepp/bitstream_writer.h>

namespace ricepp::detail {

template <unsigned FsMax, typename T>
  requires std::unsigned_integral<typename T::value_type>
[[nodiscard]] std::pair<unsigned, unsigned>
compute_best_split(T const& delta, size_t const size,
                   uint64_t const sum) noexcept {
  auto bits_for_fs = [&](auto fs) {
    auto const mask = std::numeric_limits<typename T::value_type>::max() << fs;
    unsigned bits{0};
    for (size_t i = 0; i < size; ++i) {
      bits += delta[i] & mask;
    }
    return size * (fs + 1) + (bits >> fs);
  };

  static constexpr auto const kMaxBits = std::numeric_limits<uint64_t>::digits;
  auto const start_fs =
      kMaxBits - std::min(kMaxBits, std::countl_zero(sum / size) + 2);

  auto bits0 = bits_for_fs(start_fs);
  auto bits1 = bits_for_fs(start_fs + 1);

  int cand_fs;
  int bits;
  int direction;

  if (bits1 <= bits0) [[likely]] {
    cand_fs = start_fs + 1;
    bits = bits1;
    direction = 1;
  } else {
    cand_fs = start_fs;
    bits = bits0;
    direction = -1;
  }

  if (bits0 != bits1) [[likely]] {
    while (cand_fs > 0 && cand_fs < FsMax) {
      auto const tmp = bits_for_fs(cand_fs + direction);
      if (tmp > bits) [[likely]] {
        break;
      }
      bits = tmp;
      cand_fs += direction;
    }
  }

  return std::make_pair(cand_fs, bits);
}

template <size_t MaxBlockSize, typename PixelTraits, ranges::viewable_range V,
          typename BitstreamWriter, std::unsigned_integral ValueT>
  requires std::unsigned_integral<typename PixelTraits::value_type> &&
           std::same_as<ranges::range_value_t<std::decay_t<V>>,
                        typename PixelTraits::value_type>
void encode_block(V block, BitstreamWriter& writer, PixelTraits const& traits,
                  ValueT& last_value) {
  using pixel_value_type = typename PixelTraits::value_type;
  using value_type = ValueT;
  static_assert(sizeof(pixel_value_type) <= sizeof(value_type));
  static constexpr value_type kPixelBits{PixelTraits::kBitCount};
  static constexpr value_type kFsBits{
      static_cast<value_type>(std::countr_zero(kPixelBits))};
  static constexpr value_type kFsMax{kPixelBits - 2};
  static constexpr value_type kPixelMsb{static_cast<value_type>(1)
                                        << (kPixelBits - 1)};

  std::array<pixel_value_type, MaxBlockSize> delta;
  value_type last = last_value;

  assert(block.size() <= MaxBlockSize);

  uint64_t sum{0};

  for (size_t i = 0; i < block.size(); ++i) {
    value_type const pixel = traits.read(block[i]);
    value_type const diff = static_cast<value_type>(pixel - last);
    pixel_value_type d = diff & kPixelMsb ? ~(diff << 1) : (diff << 1);
    delta[i] = d;
    sum += d;
    last = pixel;
  }

  last_value = last;

  if (sum > 0) [[likely]] {
    // Find the best bit position to split the difference values.
    auto const [fs, bits_used] =
        compute_best_split<kFsMax>(delta, block.size(), sum);

    if (fs < kFsMax && bits_used < kPixelBits * block.size()) [[likely]] {
      // Encode the difference values using Rice entropy coding.
      writer.write_bits(fs + 1, kFsBits);
      for (size_t i = 0; i < block.size(); ++i) {
        value_type const diff = delta[i];
        value_type const top = diff >> fs;
        if (top > 0) [[unlikely]] {
          writer.write_bit(0, top);
        }
        writer.write_bit(1);
        writer.write_bits(diff, fs);
      }
    } else {
      // Difference values are too large for entropy coding. Just plain copy
      // the input pixel data. This is really unlikely, so reading the input
      // pixels again is fine.
      writer.write_bits(kFsMax + 1, kFsBits);
      for (auto& b : block) {
        writer.write_bits(b, kPixelBits);
      }
    }
  } else {
    // All differences are zero, so just write a zero fs and we're done.
    writer.write_bits(0U, kFsBits);
  }
}

} // namespace ricepp::detail
