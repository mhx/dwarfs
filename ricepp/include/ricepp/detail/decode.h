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
#include <concepts>
#include <type_traits>

#include <range/v3/range/concepts.hpp>

#include <ricepp/bitstream_reader.h>
#include <ricepp/detail/compiler.h>

namespace ricepp::detail {

template <size_t MaxBlockSize, typename PixelTraits, ranges::viewable_range V,
          typename BitstreamReader, std::unsigned_integral ValueT>
  requires std::unsigned_integral<typename PixelTraits::value_type> &&
           std::same_as<ranges::range_value_t<std::decay_t<V>>,
                        typename PixelTraits::value_type>
void decode_block(V block, BitstreamReader& reader, PixelTraits const& traits,
                  ValueT& last_value) {
  using value_type = ValueT;
  static_assert(sizeof(typename PixelTraits::value_type) <= sizeof(value_type));
  static constexpr value_type kPixelBits{PixelTraits::kBitCount};
  static constexpr value_type kFsBits{
      static_cast<value_type>(std::countr_zero(kPixelBits))};
  static constexpr value_type kFsMax{kPixelBits - 2};

  value_type last = last_value;

  auto const fsp1 = reader.template read_bits<value_type>(kFsBits);

  if (fsp1 > 0) {
    if (fsp1 <= kFsMax) {
      auto const fs = fsp1 - 1;
      for (auto& b : block) {
        value_type diff = reader.find_first_set() << fs;
        diff |= reader.template read_bits<value_type>(fs);
        last += static_cast<std::make_signed_t<value_type>>(
            ((diff & 1) * value_type(-1)) ^ (diff >> 1));
        // last += static_cast<std::make_signed_t<value_type>>(
        //     (diff & 1) ? ~(diff >> 1) : (diff >> 1));
        b = traits.write(last);
      }
    } else {
      for (auto& b : block) {
        b = reader.template read_bits<value_type>(kPixelBits);
      }
      last = traits.read(block.back());
    }
  } else {
    std::fill(block.begin(), block.end(), traits.write(last));
  }

  last_value = last;
}

} // namespace ricepp::detail
