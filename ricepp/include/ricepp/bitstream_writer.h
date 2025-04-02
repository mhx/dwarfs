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
#include <cassert>
#include <concepts>
#include <iterator>
#include <type_traits>

#include <ricepp/byteswap.h>
#include <ricepp/detail/compiler.h>

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
  static constexpr size_t kBitsTypeBits{std::numeric_limits<bits_type>::digits};

  bitstream_writer(OutputIt out)
      : out_{out} {}

  RICEPP_FORCE_INLINE void write_bit(bool bit) {
    assert(bit_pos_ < kBitsTypeBits);
    write_bits_impl(bit, 1);
  }

  RICEPP_FORCE_INLINE void write_bit(bool bit, size_t repeat) {
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
    if (repeat > 0) [[likely]] {
      write_bits_impl(bits, repeat);
    }
  }

  template <std::unsigned_integral T>
  RICEPP_FORCE_INLINE void write_bits(T bits, size_t num_bits) {
    static constexpr size_t kArgBits{std::numeric_limits<T>::digits};
    assert(bit_pos_ < kBitsTypeBits);
    assert(num_bits <= kArgBits);
    if (num_bits > 0) [[likely]] {
      for (;;) {
        size_t const bits_to_write =
            std::min(num_bits, kBitsTypeBits - bit_pos_);
        write_bits_impl(bits, bits_to_write);
        bits >>= bits_to_write;
        if (num_bits == bits_to_write) [[likely]] {
          break;
        }
        num_bits -= bits_to_write;
      }
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
  RICEPP_FORCE_INLINE void write_bits_impl(bits_type bits, size_t num_bits) {
    assert(bit_pos_ + num_bits <= kBitsTypeBits);
    if (num_bits < kBitsTypeBits) [[likely]] {
      bits &= (static_cast<bits_type>(1) << num_bits) - 1;
    }
    data_ |= bits << bit_pos_;
    bit_pos_ += num_bits;
    if (bit_pos_ == kBitsTypeBits) {
      write_packet(data_);
      data_ = bits_type{};
      bit_pos_ = 0;
    }
  }

  RICEPP_FORCE_INLINE void write_packet(bits_type bits) {
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

} // namespace ricepp
