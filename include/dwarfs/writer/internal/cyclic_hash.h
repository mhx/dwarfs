/* vim:set ts=2 sw=2 sts=2 et: */
/**
 * \author     Marcus Holland-Moritz (github@mhxnet.de)
 * \copyright  Copyright (c) Marcus Holland-Moritz
 *
 * This file is part of dwarfs.
 *
 * dwarfs is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * dwarfs is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with dwarfs.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <array>
#include <bit>
#include <cassert>
#include <cstdint>
#include <stdexcept>

#include <fmt/format.h>
#include <iostream>

#include <folly/hash/Hash.h>

#include <dwarfs/compiler.h>

#include <immintrin.h> // For AVX intrinsics
#include <smmintrin.h> // For SSE4.1 intrinsics
#include <tmmintrin.h> // For SSSE3 intrinsics

namespace dwarfs::writer::internal {

class rsync_hash {
 public:
  rsync_hash() = default;

  DWARFS_FORCE_INLINE uint32_t operator()() const {
    return a_ | (uint32_t(b_) << 16);
  }

  DWARFS_FORCE_INLINE void update(uint8_t inbyte) {
    a_ += inbyte;
    b_ += a_;
    ++len_;
  }

  DWARFS_FORCE_INLINE void update(uint8_t outbyte, uint8_t inbyte) {
    a_ = a_ - outbyte + inbyte;
    b_ -= len_ * outbyte;
    b_ += a_;
  }

  DWARFS_FORCE_INLINE void clear() {
    a_ = 0;
    b_ = 0;
    len_ = 0;
  }

  static DWARFS_FORCE_INLINE constexpr uint32_t
  repeating_window(uint8_t byte, size_t length) {
    uint16_t v = static_cast<uint16_t>(byte);
    uint16_t a{static_cast<uint16_t>(v * length)};
    uint16_t b{static_cast<uint16_t>(v * (length * (length + 1)) / 2)};
    return static_cast<uint32_t>(a) | (static_cast<uint32_t>(b) << 16);
  }

 private:
  uint16_t a_{0};
  uint16_t b_{0};
  int32_t len_{0};
};

constexpr int kBShift = 7;

template <typename T>
class parallel_cyclic_hash {
 public:
  static_assert(sizeof(T) >= 4, "T must be at least 4 bytes wide");
  using value_type = T;
  static size_t constexpr hash_count = sizeof(value_type);
  static constexpr bool UseRevMix{false};

  constexpr parallel_cyclic_hash(size_t window_size)
      : shift_{std::countr_zero(window_size / sizeof(value_type))} {
    assert(std::popcount(window_size) == 1);
    assert(window_size >= hash_count);
  }

  DWARFS_FORCE_INLINE void get(uint32_t* ptr) const {
    for (size_t i = 0; i < hash_count; ++i) {
      // ptr[i] = a_[i] ^ b_[i];
      ptr[i] = a_[i] + (b_[i] >> kBShift);
      if constexpr (UseRevMix) {
        ptr[i] = folly::hash::jenkins_rev_mix32(ptr[i]);
      }
    }
  }

  DWARFS_FORCE_INLINE constexpr value_type operator()(size_t i) const {
    // return a_ | (uint32_t(b_) << 16);

    // auto rv = a_[i] ^ b_[i];
    auto rv = a_[i] + (b_[i] >> kBShift);
    if (UseRevMix) {
      rv = folly::hash::jenkins_rev_mix32(rv);
    }
    return rv;
    // return a_[i] | (static_cast<uint64_t>(b_[i]) << (8 *
    // sizeof(value_type)));
  }

  // static std::string to_string(value_type v) {
  //   char buf[sizeof(value_type)];
  //   std::memcpy(buf, &v, sizeof(value_type));
  //   std::replace(std::begin(buf), std::end(buf), '\0', '_');
  //   return std::string(buf, sizeof(value_type));
  // }

  DWARFS_FORCE_INLINE constexpr void update(uint8_t in) {
    in_ |= static_cast<value_type>(in) << (8 * num_);
    if (++num_ == hash_count) {
      update_wide(in_);
      in_ = 0;
      num_ = 0;
    }
  }

  DWARFS_FORCE_INLINE constexpr void update(uint8_t out, uint8_t in) {
    in_ |= static_cast<value_type>(in) << (8 * num_);
    out_ |= static_cast<value_type>(out) << (8 * num_);
    if (++num_ == hash_count) {
      update_wide(out_, in_);
      in_ = 0;
      out_ = 0;
      num_ = 0;
    }
  }

  DWARFS_FORCE_INLINE constexpr void update_wide(T in) {
    for (size_t i = 0; i < hash_count - 1; ++i) {
      a_[i] += combine(last_in_, in, i);
      b_[i] += a_[i];
    }

    a_[hash_count - 1] += in;
    b_[hash_count - 1] += a_[hash_count - 1];

    last_in_ = in;
  }

  DWARFS_FORCE_INLINE constexpr void update_wide(T out, T in) {
    for (size_t i = 0; i < hash_count - 1; ++i) {
      auto tmp = combine(last_out_, out, i);
      a_[i] = a_[i] - tmp + combine(last_in_, in, i);
      b_[i] -= tmp << shift_;
      b_[i] += a_[i];
    }

    a_[hash_count - 1] = a_[hash_count - 1] - out + in;
    b_[hash_count - 1] -= out << shift_;
    b_[hash_count - 1] += a_[hash_count - 1];

    last_in_ = in;
    last_out_ = out;
  }

  DWARFS_FORCE_INLINE constexpr void clear() {
    last_in_ = 0;
    last_out_ = 0;
    for (size_t i = 0; i < hash_count; ++i) {
      a_[i] = 0;
      b_[i] = 0;
    }
  }

 private:
  static constexpr DWARFS_FORCE_INLINE value_type combine(value_type a,
                                                          value_type b,
                                                          int shift) {
    int const a_rshift = 8 * (shift + 1);
    int const b_lshift = 8 * (sizeof(value_type) - (shift + 1));
    value_type const a_rshifted =
        shift + 1 == sizeof(value_type) ? 0 : a >> a_rshift;
    value_type const b_lshifted = b << b_lshift;
    value_type r = a_rshifted | b_lshifted;

    return r;
  }

  value_type in_{0};
  value_type out_{0};
  int num_{0};
  value_type last_in_{0};
  value_type last_out_{0};
  std::array<value_type, hash_count> a_{};
  std::array<value_type, hash_count> b_{};
  int const shift_{0};
};

class cyclic_hash_sse {
 public:
  using value_type = uint32_t;
  using reg_type = __m128i;
  static size_t constexpr hash_count = 4;
  static constexpr bool UseRevMix{false};

  cyclic_hash_sse(size_t window_size)
      : shift_{std::countr_zero(window_size / sizeof(value_type))} {
    assert(std::popcount(window_size) == 1);
    assert(window_size >= hash_count);
  }

  DWARFS_FORCE_INLINE value_type operator()(size_t i) const {
    // reg_type v = jenkins_rev_mix32(_mm_xor_si128(a_, b_));
    reg_type v = jenkins_rev_mix32(_mm_add_epi32(a_, _mm_srli_epi32(b_, kBShift)));
    std::array<value_type, 4> tmp;
    _mm_storeu_si128(reinterpret_cast<reg_type*>(tmp.data()), v);
    return tmp[i];
  }

  DWARFS_FORCE_INLINE void get(uint32_t* ptr) const {
    // reg_type v = jenkins_rev_mix32(_mm_xor_si128(a_, b_));
    reg_type v = jenkins_rev_mix32(_mm_add_epi32(a_, _mm_srli_epi32(b_, kBShift)));
    _mm_storeu_si128(reinterpret_cast<reg_type*>(ptr), v);
  }

  static DWARFS_FORCE_INLINE reg_type jenkins_rev_mix32(reg_type key) {
    if constexpr (UseRevMix) {
      key = _mm_add_epi32(key, _mm_slli_epi32(key, 12));
      key = _mm_xor_si128(key, _mm_srli_epi32(key, 22));
      key = _mm_add_epi32(key, _mm_slli_epi32(key, 4));
      key = _mm_xor_si128(key, _mm_srli_epi32(key, 9));
      key = _mm_add_epi32(key, _mm_slli_epi32(key, 10));
      key = _mm_xor_si128(key, _mm_srli_epi32(key, 2));
      key = _mm_add_epi32(key, _mm_slli_epi32(key, 7));
      key = _mm_add_epi32(key, _mm_slli_epi32(key, 12));
    }
    return key;
  }

  DWARFS_FORCE_INLINE void update(uint8_t in) {
    in_ |= static_cast<value_type>(in) << (8 * num_);
    if (++num_ == hash_count) {
      update_wide(in_);
      in_ = 0;
      num_ = 0;
    }
  }

  DWARFS_FORCE_INLINE void update(uint8_t out, uint8_t in) {
    in_ |= static_cast<value_type>(in) << (8 * num_);
    out_ |= static_cast<value_type>(out) << (8 * num_);
    if (++num_ == hash_count) {
      update_wide(out_, in_);
      in_ = 0;
      out_ = 0;
      num_ = 0;
    }
  }

  DWARFS_FORCE_INLINE void update_wide(uint32_t in) {
    // std::cout << "  last_inout_ = ";
    // print_m128i_u32(last_inout_);
    last_inout_ = _mm_insert_epi32(last_inout_, in, 0);
    reg_type vin = _mm_shuffle_epi8(last_inout_, combine_);
    a_ = _mm_add_epi32(a_, vin);
    b_ = _mm_add_epi32(b_, a_);

    // for (size_t i = 0; i < hash_count - 1; ++i) {
    //   a_[i] += combine(last_in_, in, i);
    //   b_[i] += a_[i];
    // }

    // a_[hash_count - 1] += in;
    // b_[hash_count - 1] += a_[hash_count - 1];

    // last_in_ = in;

    // last_inout_ = _mm_slli_si128(last_inout_, 4);
    last_inout_ = _mm_slli_epi64(last_inout_, 32);
    // std::cout << "             -> ";
    // print_m128i_u32(last_inout_);
  }

  DWARFS_FORCE_INLINE void update_wide(uint32_t out, uint32_t in) {
    // std::cout << "  last_inout_ = ";
    // print_m128i_u32(last_inout_);
    last_inout_ = _mm_insert_epi32(last_inout_, in, 0);
    last_inout_ = _mm_insert_epi32(last_inout_, out, 2);
    // std::cout << "             -> ";
    // print_m128i_u32(last_inout_);
    reg_type vin = _mm_shuffle_epi8(last_inout_, combine_);
    reg_type vout = _mm_srli_si128(last_inout_, 8);
    vout = _mm_shuffle_epi8(vout, combine_);
    a_ = _mm_sub_epi32(a_, vout);
    vout = _mm_slli_epi32(vout, shift_);
    a_ = _mm_add_epi32(a_, vin);
    b_ = _mm_sub_epi32(b_, vout);
    b_ = _mm_add_epi32(b_, a_);

    // for (size_t i = 0; i < hash_count - 1; ++i) {
    //   auto tmp = combine(last_out_, out, i);
    //   a_[i] = a_[i] - tmp + combine(last_in_, in, i);
    //   b_[i] -= tmp << shift_;
    //   b_[i] += a_[i];
    // }

    // a_[hash_count - 1] = a_[hash_count - 1] - out + in;
    // b_[hash_count - 1] -= out << shift_;
    // b_[hash_count - 1] += a_[hash_count - 1];

    // last_in_ = in;
    // last_out_ = out;

    // last_inout_ = _mm_slli_si128(last_inout_, 4);
    last_inout_ = _mm_slli_epi64(last_inout_, 32);

    // std::cout << "             -> ";
    // print_m128i_u32(last_inout_);
  }

  static DWARFS_FORCE_INLINE constexpr uint32_t
  repeating_window(uint8_t byte, size_t length) {
    uint32_t v = static_cast<uint32_t>(byte);
    v = v | (v << 8) | (v << 16) | (v << 24);
    length /= sizeof(uint32_t);
    uint32_t a{static_cast<uint32_t>(v * length)};
    uint32_t b{static_cast<uint32_t>(v * (length * (length + 1)) / 2)};
    // uint32_t rv = a ^ b;
    uint32_t rv = a + (b >> kBShift);
    if constexpr (UseRevMix) {
      rv = folly::hash::jenkins_rev_mix32(rv);
    }
    return rv;
  }

  DWARFS_FORCE_INLINE void clear() {
    last_inout_ = _mm_setzero_si128();
    a_ = _mm_setzero_si128();
    b_ = _mm_setzero_si128();
    in_ = 0;
    out_ = 0;
    num_ = 0;
  }

 private:
  // static std::string to_string(value_type v) {
  //   char buf[sizeof(value_type)];
  //   std::memcpy(buf, &v, sizeof(value_type));
  //   std::replace(std::begin(buf), std::end(buf), '\0', '_');
  //   return std::string(buf, sizeof(value_type));
  // }

  // void print_m128i_u32(reg_type var)
  // {
  //     uint32_t val[4];
  //     _mm_storeu_si128(reinterpret_cast<reg_type*>(val), var);
  //     for (int i = 0; i < 4; ++i)
  //     {
  //         std::cout << fmt::format("0x{:08x} [{}], ", val[i],
  //         to_string(val[i]));
  //     }
  //     std::cout << "\n";
  // }

  static std::array<uint8_t, 16> constexpr kCombineMask = {
      5, 6, 7, 0, // a0: (a1 >> 8) | (x << 24)
      6, 7, 0, 1, // a1: (a1 >> 16) | (x << 16)
      7, 0, 1, 2, // a2: (a1 >> 24) | (x << 8)
      0, 1, 2, 3  // a3: x
  };

  value_type in_{0};
  value_type out_{0};
  int num_{0};
  reg_type a_{_mm_setzero_si128()};
  reg_type b_{_mm_setzero_si128()};
  // [last_out, new_out, last_in, new_in]
  reg_type last_inout_{_mm_setzero_si128()};
  reg_type combine_{
      _mm_loadu_si128(reinterpret_cast<reg_type const*>(kCombineMask.data()))};
  int const shift_{0};
};

} // namespace dwarfs::writer::internal
