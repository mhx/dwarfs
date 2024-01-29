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
[[nodiscard]] std::pair<unsigned, unsigned>
compute_best_split(T const& delta, size_t size) noexcept {
  auto bits_for_fs = [&](unsigned fs) {
    unsigned bits = size * (fs + 1);
    for (size_t i = 0; i < size; ++i) {
      bits += delta[i] >> fs;
    }
    return bits;
  };

  unsigned cand_fs{0};
  unsigned bits = bits_for_fs(0);
  while (cand_fs < FsMax) {
    unsigned const bits2 = bits_for_fs(cand_fs + 1);
    if (bits2 > bits) {
      break;
    }
    bits = bits2;
    ++cand_fs;
  }
  return std::make_pair(cand_fs, bits);
}

template <size_t MaxBlockSize, typename PixelTraits, ranges::viewable_range V,
          bitstream_writer_type W>
  requires std::unsigned_integral<typename PixelTraits::value_type> &&
           std::same_as<ranges::range_value_t<std::decay_t<V>>,
                        typename PixelTraits::value_type>
void encode_block(V block, W& writer, PixelTraits const& traits,
                  typename PixelTraits::value_type& last_value) {
  using pixel_value_type = typename PixelTraits::value_type;
  static constexpr unsigned kPixelBits{PixelTraits::kBitCount};
  static constexpr unsigned kFsBits{std::countr_zero(kPixelBits)};
  static constexpr unsigned kFsMax{kPixelBits - 2};
  static constexpr pixel_value_type kPixelMsb{static_cast<pixel_value_type>(1)
                                              << (kPixelBits - 1)};

  std::array<pixel_value_type, MaxBlockSize> delta;
  auto last = last_value;

  assert(block.size() <= MaxBlockSize);

  uint64_t sum{0};
  for (size_t i = 0; i < block.size(); ++i) {
    auto const pixel = traits.read(block[i]);
    auto const diff = static_cast<pixel_value_type>(pixel - last);
    delta[i] = diff & kPixelMsb ? ~(diff << 1) : (diff << 1);
    sum += delta[i];
    last = pixel;
  }

  last_value = last;

  if (sum == 0) [[unlikely]] {
    // All differences are zero, so just write a zero fs and we're done.
    writer.write_bits(0U, kFsBits);
  } else {
    // Find the best bit position to split the difference values.
    auto const [fs, bits_used] =
        compute_best_split<kFsMax>(delta, block.size());

    if (fs >= kFsMax || bits_used >= kPixelBits * block.size()) [[unlikely]] {
      // Difference values are too large for entropy coding. Just plain copy
      // the input pixel data. This is really unlikely, so reading the input
      // pixels again is fine.
      writer.write_bits(kFsMax + 1, kFsBits);
      for (size_t i = 0; i < block.size(); ++i) {
        writer.write_bits(block[i], kPixelBits);
      }
    } else {
      // Encode the difference values using Rice entropy coding.
      writer.write_bits(fs + 1, kFsBits);
      for (size_t i = 0; i < block.size(); ++i) {
        pixel_value_type const diff = delta[i];
        pixel_value_type const top = diff >> fs;
        if (top > 0) {
          writer.write_bit(0, top);
        }
        writer.write_bit(1);
        writer.write_bits(diff, fs);
      }
    }
  }
}

} // namespace ricepp::detail
