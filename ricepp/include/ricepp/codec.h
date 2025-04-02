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
#include <bit>
#include <cassert>
#include <concepts>
#include <span>

#include <range/v3/view/all.hpp>
#include <range/v3/view/chunk.hpp>
#include <range/v3/view/drop.hpp>
#include <range/v3/view/stride.hpp>

#include <ricepp/detail/decode.h>
#include <ricepp/detail/encode.h>

namespace ricepp {

template <size_t MaxBlockSize, size_t ComponentStreamCount,
          typename PixelTraits>
  requires std::unsigned_integral<typename PixelTraits::value_type>
class codec final {
 public:
  using pixel_traits = PixelTraits;
  using pixel_value_type = typename pixel_traits::value_type;
  static constexpr size_t kMaxBlockSize = MaxBlockSize;
  static constexpr size_t kComponentStreamCount = ComponentStreamCount;

  codec(size_t block_size, PixelTraits const& traits) noexcept
      : block_size_{block_size}
      , traits_{traits} {}

  template <typename BitstreamWriter>
  void encode(std::span<pixel_value_type const> input,
              BitstreamWriter& writer) const {
    using value_type = uint_fast32_t;

    assert(input.size() % kComponentStreamCount == 0);

    if constexpr (kComponentStreamCount == 1) {
      value_type last_value;

      last_value = traits_.read(input[0]);
      writer.write_bits(last_value, PixelTraits::kBitCount);

      for (auto pixels :
           ranges::views::all(input) | ranges::views::chunk(block_size_)) {
        detail::encode_block<kMaxBlockSize, pixel_traits>(pixels, writer,
                                                          traits_, last_value);
      }
    } else {
      std::array<value_type, kComponentStreamCount> last_value;

      for (size_t i = 0; i < kComponentStreamCount; ++i) {
        last_value[i] = traits_.read(input[i]);
        writer.write_bits(last_value[i], PixelTraits::kBitCount);
      }

      for (auto pixels :
           ranges::views::all(input) |
               ranges::views::chunk(kComponentStreamCount * block_size_)) {
        for (size_t i = 0; i < kComponentStreamCount; ++i) {
          detail::encode_block<kMaxBlockSize, pixel_traits>(
              pixels | ranges::views::drop(i) |
                  ranges::views::stride(kComponentStreamCount),
              writer, traits_, last_value[i]);
        }
      }
    }

    writer.flush();
  }

  template <typename BitstreamReader>
  void
  decode(std::span<pixel_value_type> output, BitstreamReader& reader) const {
    using value_type = uint_fast32_t;

    assert(output.size() % kComponentStreamCount == 0);

    if constexpr (kComponentStreamCount == 1) {
      value_type last_value;

      last_value =
          reader.template read_bits<value_type>(PixelTraits::kBitCount);

      for (auto pixels :
           ranges::views::all(output) | ranges::views::chunk(block_size_)) {
        detail::decode_block<kMaxBlockSize, pixel_traits>(pixels, reader,
                                                          traits_, last_value);
      }
    } else {
      std::array<value_type, kComponentStreamCount> last_value;

      for (size_t i = 0; i < kComponentStreamCount; ++i) {
        last_value[i] =
            reader.template read_bits<value_type>(PixelTraits::kBitCount);
      }

      for (auto pixels :
           ranges::views::all(output) |
               ranges::views::chunk(kComponentStreamCount * block_size_)) {
        for (size_t i = 0; i < kComponentStreamCount; ++i) {
          detail::decode_block<kMaxBlockSize, pixel_traits>(
              pixels | ranges::views::drop(i) |
                  ranges::views::stride(kComponentStreamCount),
              reader, traits_, last_value[i]);
        }
      }
    }
  }

  [[nodiscard]] size_t worst_case_bit_count(size_t pixel_count) const noexcept {
    static constexpr size_t const kFsBits{
        static_cast<size_t>(std::countr_zero(pixel_traits::kBitCount))};
    assert(pixel_count % kComponentStreamCount == 0);
    pixel_count /= kComponentStreamCount;
    size_t num = pixel_traits::kBitCount; // initial value
    num += kFsBits * ((pixel_count + block_size_ - 1) / block_size_); // fs
    num += pixel_traits::kBitCount * pixel_count; // plain values
    return num * kComponentStreamCount;
  }

 private:
  size_t const block_size_;
  PixelTraits const& traits_;
};

} // namespace ricepp
