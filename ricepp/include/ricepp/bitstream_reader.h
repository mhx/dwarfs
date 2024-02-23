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
#include <bit>
#include <cassert>
#include <concepts>
#include <cstring>
#include <iterator>
#include <stdexcept>

#include <ricepp/byteswap.h>

namespace ricepp {

template <std::input_iterator InputIt>
  requires std::same_as<typename std::iterator_traits<InputIt>::value_type,
                        uint8_t>
class bitstream_reader final {
 public:
  using iterator_type = InputIt;
  using bits_type = uint64_t;
  static constexpr size_t kBitsTypeBits{std::numeric_limits<bits_type>::digits};

  bitstream_reader(InputIt beg, InputIt end)
      : beg_{std::move(beg)}
      , end_{std::move(end)} {}

  bool read_bit() { return read_bits_impl(1); }

  template <std::unsigned_integral T>
  T read_bits(size_t num_bits) {
    assert(num_bits <= std::numeric_limits<T>::digits);
    T bits = 0;
    uint16_t pos = 0;
    if (num_bits > 0) {
      for (;;) {
        size_t const remain = kBitsTypeBits - bit_pos_;
        if (num_bits <= remain) {
          bits |= static_cast<T>(read_bits_impl(num_bits)) << pos;
          break;
        }
        bits |= static_cast<T>(read_bits_impl(remain)) << pos;
        num_bits -= remain;
        pos += remain;
      }
    }
    return bits;
  }

  size_t find_first_set() {
    size_t zeros = 0;
    if (bit_pos_ != 0) {
      if (peek_bit()) [[likely]] {
        skip_bits(1);
        return zeros;
      }
      size_t const remaining_bits = kBitsTypeBits - bit_pos_;
      bits_type const bits = peek_bits(remaining_bits);
      size_t const ffs = std::countr_zero(bits);
      if (ffs < remaining_bits) [[likely]] {
        skip_bits(ffs + 1);
        return ffs;
      }
      bit_pos_ = 0;
      zeros += remaining_bits;
    }
    for (;;) {
      bits_type const bits = read_packet();
      if (bits != bits_type{}) [[likely]] {
        size_t const ffs = std::countr_zero(bits);
        assert(ffs < kBitsTypeBits);
        if (ffs + 1 == kBitsTypeBits) [[unlikely]] {
          bit_pos_ = 0;
        } else {
          data_ = bits;
          bit_pos_ = ffs + 1;
        }
        return zeros + ffs;
      }
      zeros += kBitsTypeBits;
    }
  }

 private:
  bits_type read_bits_impl(size_t num_bits) {
    auto bits = peek_bits(num_bits);
    skip_bits(num_bits);
    return bits;
  }

  void skip_bits(size_t num_bits) {
    assert(bit_pos_ + num_bits <= kBitsTypeBits);
    bit_pos_ += num_bits;
    bit_pos_ &= kBitsTypeBits - 1;
  }

  bool peek_bit() {
    assert(bit_pos_ > 0 && bit_pos_ < kBitsTypeBits);
    return (data_ >> bit_pos_) & 1;
  }

  bits_type peek_bits(size_t num_bits) {
    assert(bit_pos_ + num_bits <= kBitsTypeBits);
    if (bit_pos_ == 0) [[unlikely]] {
      data_ = read_packet();
    }
    // The remainder of this function is equivalent to:
    //
    //   return _bextr_u64(data_, bit_pos_, num_bits);
    //
    // However, in practice, at least clang generates code that is as fast
    // as the intrinsic, so we use the following code for portability.
    bits_type bits = data_ >> bit_pos_;
    if (num_bits < kBitsTypeBits) [[likely]] {
      bits &= (static_cast<bits_type>(1) << num_bits) - 1;
    }
    return bits;
  }

  bits_type read_packet() {
    if (beg_ == end_) [[unlikely]] {
      throw std::out_of_range{"bitstream_reader::read_packet"};
    }
    return read_packet_nocheck();
  }

  bits_type read_packet_nocheck()
    requires std::contiguous_iterator<iterator_type>
  {
    bits_type bits{};
    if (auto const remain = end_ - beg_; remain >= sizeof(bits_type))
        [[likely]] {
      std::memcpy(&bits, &*beg_, sizeof(bits_type));
      beg_ += sizeof(bits_type);
    } else {
      std::memcpy(&bits, &*beg_, remain);
      beg_ = end_;
    }
    return byteswap<std::endian::little>(bits);
  }

  bits_type read_packet_nocheck()
    requires(!std::contiguous_iterator<iterator_type>)
  {
    bits_type bits{};
    auto bits_ptr = reinterpret_cast<uint8_t*>(&bits);
    for (size_t i = 0; i < sizeof(bits_type); ++i) {
      if (beg_ == end_) [[unlikely]] {
        break;
      }
      bits_ptr[i] = *beg_++;
    }
    return byteswap<std::endian::little>(bits);
  }

  bits_type data_{};
  size_t bit_pos_{0};
  iterator_type beg_, end_;
};

} // namespace ricepp
