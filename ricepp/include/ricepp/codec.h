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

  template <bitstream_writer_type W>
  void encode(std::span<pixel_value_type const> input, W& writer) const {
    assert(input.size() % kComponentStreamCount == 0);

    std::array<pixel_value_type, kComponentStreamCount> last_value;

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

    writer.flush();
  }

  template <bitstream_reader_type R>
  void decode(std::span<pixel_value_type> output, R& reader) const {
    assert(output.size() % kComponentStreamCount == 0);

    std::array<pixel_value_type, kComponentStreamCount> last_value;

    for (size_t i = 0; i < kComponentStreamCount; ++i) {
      last_value[i] =
          reader.template read_bits<pixel_value_type>(PixelTraits::kBitCount);
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

  [[nodiscard]] size_t worst_case_bit_count(size_t pixel_count) const noexcept {
    static constexpr size_t const kFsBits{
        std::countr_zero(pixel_traits::kBitCount)};
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
