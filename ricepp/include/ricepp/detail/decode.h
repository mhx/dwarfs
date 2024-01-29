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
#include <concepts>
#include <type_traits>

#include <range/v3/range/concepts.hpp>

#include <ricepp/bitstream_reader.h>

namespace ricepp::detail {

template <size_t MaxBlockSize, typename PixelTraits, ranges::viewable_range V,
          bitstream_reader_type R>
  requires std::unsigned_integral<typename PixelTraits::value_type> &&
           std::same_as<ranges::range_value_t<std::decay_t<V>>,
                        typename PixelTraits::value_type>
void decode_block(V block, R& reader, PixelTraits const& traits,
                  typename PixelTraits::value_type& last_value) {
  using pixel_value_type = typename PixelTraits::value_type;
  static constexpr unsigned kPixelBits{PixelTraits::kBitCount};
  static constexpr unsigned kFsBits{std::countr_zero(kPixelBits)};
  static constexpr unsigned kFsMax{kPixelBits - 2};

  auto last = last_value;

  auto const fsp1 = reader.template read_bits<unsigned>(kFsBits);

  if (fsp1 == 0) [[unlikely]] {
    std::fill(block.begin(), block.end(), traits.write(last));
  } else if (fsp1 > kFsMax) [[unlikely]] {
    for (size_t i = 0; i < block.size(); ++i) {
      block[i] = reader.template read_bits<pixel_value_type>(kPixelBits);
    }
    last = traits.read(block.back());
  } else {
    auto const fs = fsp1 - 1;
    for (size_t i = 0; i < block.size(); ++i) {
      pixel_value_type diff = reader.find_first_set() << fs;
      if (fs > 0) {
        diff |= reader.template read_bits<pixel_value_type>(fs);
      }
      last += static_cast<std::make_signed_t<pixel_value_type>>(
          (diff & 1) ? ~(diff >> 1) : (diff >> 1));
      block[i] = traits.write(last);
    }
  }

  last_value = last;
}

} // namespace ricepp::detail
