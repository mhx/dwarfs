/* vim:set ts=2 sw=2 sts=2 et: */
/**
 * \author     Marcus Holland-Moritz (github@mhxnet.de)
 * \copyright  Copyright (c) Marcus Holland-Moritz
 *
 * This file is part of dwarfs.
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

#include <bit>
#include <compare>
#include <concepts>
#include <cstdint>
#include <type_traits>

namespace dwarfs {

template <std::unsigned_integral T, std::endian Endian>
class boxed_endian {
 public:
  static_assert(Endian == std::endian::little || Endian == std::endian::big);

  constexpr boxed_endian() = default;
  constexpr explicit boxed_endian(T v) noexcept
      : raw_{swap(v)} {}

  constexpr operator T() const noexcept { return swap(raw_); }

  template <typename E>
    requires std::is_enum_v<E>
  constexpr explicit operator E() const noexcept {
    return static_cast<E>(swap(raw_));
  }

  constexpr T load() const noexcept { return swap(raw_); }

  constexpr boxed_endian& operator=(T v) noexcept {
    raw_ = swap(v);
    return *this;
  }

  friend constexpr auto
  operator<=>(boxed_endian lhs, boxed_endian rhs) noexcept {
    return static_cast<T>(lhs) <=> static_cast<T>(rhs);
  }

  friend constexpr bool
  operator==(boxed_endian lhs, boxed_endian rhs) noexcept {
    return static_cast<T>(lhs) == static_cast<T>(rhs);
  }

 private:
  T raw_{};

  static constexpr T swap(T value) noexcept {
    if constexpr (std::endian::native == Endian || sizeof(T) == 1) {
      return value;
    } else {
      static_assert(sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8);
#ifdef _MSC_VER
      if constexpr (sizeof(T) == 2) {
        return _byteswap_ushort(value);
      } else if constexpr (sizeof(T) == 4) {
        return _byteswap_ulong(value);
      } else if constexpr (sizeof(T) == 8) {
        return _byteswap_uint64(value);
      }
#else
      if constexpr (sizeof(T) == 2) {
        return __builtin_bswap16(value);
      } else if constexpr (sizeof(T) == 4) {
        return __builtin_bswap32(value);
      } else if constexpr (sizeof(T) == 8) {
        return __builtin_bswap64(value);
      }
#endif
    }
  }
};

using uint16le_t = boxed_endian<uint16_t, std::endian::little>;
using uint32le_t = boxed_endian<uint32_t, std::endian::little>;
using uint64le_t = boxed_endian<uint64_t, std::endian::little>;

using uint16be_t = boxed_endian<uint16_t, std::endian::big>;
using uint32be_t = boxed_endian<uint32_t, std::endian::big>;
using uint64be_t = boxed_endian<uint64_t, std::endian::big>;

static_assert(sizeof(uint16le_t) == 2);
static_assert(sizeof(uint32le_t) == 4);
static_assert(sizeof(uint64le_t) == 8);

static_assert(sizeof(uint16be_t) == 2);
static_assert(sizeof(uint32be_t) == 4);
static_assert(sizeof(uint64be_t) == 8);

} // namespace dwarfs
