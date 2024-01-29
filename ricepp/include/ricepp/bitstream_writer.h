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
#include <cassert>
#include <concepts>
#include <iterator>
#include <type_traits>

#include <ricepp/byteswap.h>

namespace ricepp {

namespace detail {

template <typename T>
struct output_iterator_traits : std::iterator_traits<T> {};

template <typename Container>
struct output_iterator_traits<std::back_insert_iterator<Container>> {
  using value_type = typename Container::value_type;
};

template <typename OutputIt>
using output_iterator_value_type =
    typename output_iterator_traits<OutputIt>::value_type;

} // namespace detail

template <std::output_iterator<uint8_t> OutputIt>
class bitstream_writer final {
 public:
  using iterator_type = OutputIt;
  using bits_type = uint64_t;
  static constexpr size_t kBitsTypeBits = sizeof(bits_type) * 8;

  bitstream_writer(OutputIt out)
      : out_{out} {}

  void write_bit(bool bit) {
    assert(bit_pos_ < sizeof(bits_type) * 8);
    write_bits_impl(bit, 1);
  }

  void write_bit(bool bit, size_t repeat) {
    bits_type const bits = bit ? ~bits_type{} : bits_type{};
    if (bit_pos_ != 0) [[likely]] {
      auto remaining_bits = kBitsTypeBits - bit_pos_;
      if (repeat > remaining_bits) [[unlikely]] {
        write_bits_impl(bits, remaining_bits);
        repeat -= remaining_bits;
      }
    }
    while (repeat > kBitsTypeBits) [[unlikely]] {
      write_packet(bits);
      repeat -= kBitsTypeBits;
    }
    if (repeat > 0) {
      write_bits_impl(bits, repeat);
    }
  }

  template <std::unsigned_integral T>
  void write_bits(T bits, size_t num_bits) {
    assert(bit_pos_ < kBitsTypeBits);
    assert(num_bits <= sizeof(T) * 8);
    while (num_bits > 0) {
      size_t const bits_to_write = std::min(num_bits, kBitsTypeBits - bit_pos_);
      write_bits_impl(bits, bits_to_write);
      bits >>= bits_to_write;
      num_bits -= bits_to_write;
    }
  }

  size_t flush() {
    size_t const bits_flushed = bit_pos_;

    if (bits_flushed > 0) {
      write_packet(data_);
      data_ = bits_type{};
      bit_pos_ = 0;
    }

    return bits_flushed;
  }

  iterator_type iterator() const { return out_; }

 private:
  void write_bits_impl(bits_type bits, size_t num_bits) {
    assert(bit_pos_ + num_bits <= kBitsTypeBits);
    if (num_bits < kBitsTypeBits) {
      bits &= (static_cast<bits_type>(1) << num_bits) - 1;
    }
    data_ |= bits << bit_pos_;
    bit_pos_ += num_bits;
    if (bit_pos_ == sizeof(bits_type) * 8) {
      write_packet(data_);
      data_ = bits_type{};
      bit_pos_ = 0;
    }
  }

  void write_packet(bits_type bits) {
    size_t const to_copy =
        bit_pos_ == 0 ? sizeof(bits_type) : (bit_pos_ + 7) / 8;
    bits = byteswap<std::endian::little>(bits);
    auto const bytes = reinterpret_cast<uint8_t const*>(&bits);
    out_ = std::copy_n(bytes, to_copy, out_);
  }

  bits_type data_{};
  size_t bit_pos_{0};
  iterator_type out_;
};

template <typename T>
concept bitstream_writer_type =
    std::same_as<T, bitstream_writer<typename T::iterator_type>>;

} // namespace ricepp
