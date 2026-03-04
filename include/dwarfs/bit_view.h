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
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>
#include <utility>

#include <dwarfs/endian.h>

namespace dwarfs {

struct bit_range {
  std::size_t bit_offset{};
  std::size_t bit_width{};
};

namespace detail {

template <typename T>
concept non_bool_integral =
    !std::is_reference_v<T> && std::integral<std::remove_cv_t<T>> &&
    !std::same_as<std::remove_cv_t<T>, bool>;

template <typename T>
concept byte_storage_element =
    !std::is_reference_v<T> &&
    (std::same_as<std::remove_cv_t<T>, std::byte> ||
     (std::unsigned_integral<std::remove_cv_t<T>> && sizeof(T) == 1));

template <typename T>
concept uword_storage_element =
    !std::is_reference_v<T> && std::unsigned_integral<std::remove_cv_t<T>> &&
    sizeof(T) > 1;

template <typename T>
concept storage_element = byte_storage_element<T> || uword_storage_element<T>;

template <std::unsigned_integral U>
[[nodiscard]] constexpr auto bit_mask(std::size_t width) noexcept -> U {
  constexpr auto bits = std::numeric_limits<U>::digits;
  if (std::cmp_less(width, bits)) {
    return static_cast<U>((U{1} << width) - 1);
  }
  return std::numeric_limits<U>::max();
}

template <std::unsigned_integral U>
struct located_bits {
  std::size_t chunk0_byte{};
  unsigned shift{};
};

template <std::unsigned_integral U>
[[nodiscard]] constexpr auto
locate(std::size_t bit_offset) noexcept -> located_bits<U> {
  constexpr std::size_t chunk_bytes = sizeof(U);
  static_assert(std::has_single_bit(chunk_bytes),
                "integral sizes must be a power of two");

  std::size_t const byte0 = bit_offset >> 3;
  std::size_t const chunk0_byte = byte0 & ~(chunk_bytes - 1);

  unsigned const bit_in_byte = bit_offset & 7;
  unsigned const shift = 8 * (byte0 - chunk0_byte) + bit_in_byte;

  return {chunk0_byte, shift};
}

template <storage_element S>
class storage_accessor;

template <byte_storage_element S>
class storage_accessor<S> {
  using byte_ptr_t =
      std::conditional_t<std::is_const_v<S>, std::byte const*, std::byte*>;

 public:
  explicit storage_accessor(S* p) noexcept
      : bytes_(reinterpret_cast<byte_ptr_t>(p)) {}

  template <std::unsigned_integral U>
  [[nodiscard]] auto load(std::size_t byte_offset) const noexcept -> U {
    U v{};
    std::memcpy(&v, bytes_ + byte_offset, sizeof(U));
    return convert<std::endian::little>(v);
  }

  template <std::unsigned_integral U>
  void store(std::size_t byte_offset, U value) noexcept
    requires(!std::is_const_v<S>)
  {
    value = convert<std::endian::little>(value);
    std::memcpy(bytes_ + byte_offset, &value, sizeof(U));
  }

 private:
  byte_ptr_t bytes_{};
};

template <uword_storage_element S>
class storage_accessor<S> {
  using storage_t = S;

 public:
  explicit storage_accessor(S* p) noexcept
      : storage_(p) {}

  template <std::unsigned_integral U>
    requires(sizeof(U) == sizeof(storage_t))
  [[nodiscard]] auto load(std::size_t byte_offset) const noexcept -> U {
    return convert<std::endian::little>(
        storage_[byte_offset / sizeof(storage_t)]);
  }

  template <std::unsigned_integral U>
    requires(sizeof(U) == sizeof(storage_t))
  void store(std::size_t byte_offset, U value) noexcept
    requires(!std::is_const_v<S>)
  {
    storage_[byte_offset / sizeof(storage_t)] =
        convert<std::endian::little>(value);
  }

 private:
  S* storage_{};
};

template <non_bool_integral I>
[[nodiscard]] constexpr auto
to_unsigned(I v) noexcept -> std::make_unsigned_t<I> {
  return static_cast<std::make_unsigned_t<I>>(v); // well-defined modulo 2^N
}

template <std::unsigned_integral U>
[[nodiscard]] constexpr auto
sign_extend(U v, size_t used_bit_count) noexcept -> std::make_signed_t<U> {
  using S = std::make_signed_t<U>;
  size_t const shift_bits = std::numeric_limits<U>::digits - used_bit_count;
  return (static_cast<S>(v << shift_bits)) >> shift_bits;
}

} // namespace detail

template <detail::storage_element Storage>
  requires(!std::is_reference_v<Storage>)
class bit_view {
  using bit_access_t = std::conditional_t<sizeof(Storage) == 1, std::uint8_t,
                                          std::remove_cv_t<Storage>>;

 public:
  static constexpr bool is_const = std::is_const_v<Storage>;
  using storage_type = Storage;

  explicit bit_view(Storage* p) noexcept
      : access_(p) {}

  [[nodiscard]] auto test(std::size_t bit_index) const noexcept -> bool {
    auto const loc = detail::locate<bit_access_t>(bit_index);
    auto const b = access_.template load<bit_access_t>(loc.chunk0_byte);
    return ((b >> loc.shift) & 0x1) != 0;
  }

  void set(std::size_t bit_index) noexcept
    requires(!is_const)
  {
    auto const loc = detail::locate<bit_access_t>(bit_index);
    auto b = access_.template load<bit_access_t>(loc.chunk0_byte);
    b |= bit_access_t{1} << loc.shift;
    access_.template store<bit_access_t>(loc.chunk0_byte, b);
  }

  void clear(std::size_t bit_index) noexcept
    requires(!is_const)
  {
    auto const loc = detail::locate<bit_access_t>(bit_index);
    auto b = access_.template load<bit_access_t>(loc.chunk0_byte);
    b &= ~(bit_access_t{1} << loc.shift);
    access_.template store<bit_access_t>(loc.chunk0_byte, b);
  }

  template <detail::non_bool_integral T>
  [[nodiscard]] auto read(bit_range r) const noexcept -> T {
    using U = std::make_unsigned_t<T>;

    if constexpr (detail::uword_storage_element<Storage>) {
      static_assert(sizeof(U) == sizeof(Storage),
                    "for unsigned-integral storage, read<T> requires sizeof(T) "
                    "== sizeof(Storage)");
    }

    constexpr std::size_t chunk_bits = std::numeric_limits<U>::digits;

    if (r.bit_width == 0) {
      return 0;
    }

    auto const loc = detail::locate<U>(r.bit_offset);
    unsigned const shift = loc.shift;

    U value;
    auto const a = access_.template load<U>(loc.chunk0_byte);

    if (shift + r.bit_width <= chunk_bits) {
      value = a >> shift;
    } else {
      auto const b = access_.template load<U>(loc.chunk0_byte + sizeof(U));
      auto const shift_b = static_cast<unsigned>(chunk_bits - shift);
      // NOLINTNEXTLINE(clang-analyzer-core.BitwiseShift)
      value = (a >> shift) | (b << shift_b);
    }

    value &= detail::bit_mask<U>(r.bit_width);

    if constexpr (std::signed_integral<T>) {
      return detail::sign_extend(value, r.bit_width);
    } else {
      return value;
    }
  }

  template <detail::non_bool_integral T>
  void write(bit_range r, T const v) noexcept
    requires(!is_const)
  {
    using U = std::make_unsigned_t<T>;

    if constexpr (detail::uword_storage_element<Storage>) {
      static_assert(sizeof(U) == sizeof(Storage),
                    "for unsigned-integral storage, write<T> requires "
                    "sizeof(T) == sizeof(Storage)");
    }

    constexpr std::size_t chunk_bits = std::numeric_limits<U>::digits;

    if (r.bit_width == 0) {
      return;
    }

    auto const value_mask = detail::bit_mask<U>(r.bit_width);
    U value = detail::to_unsigned<T>(v);
    value &= value_mask;

    auto const loc = detail::locate<U>(r.bit_offset);
    unsigned const shift = loc.shift;

    if (shift + r.bit_width <= chunk_bits) {
      auto a = access_.template load<U>(loc.chunk0_byte);
      a = (a & ~(value_mask << shift)) | (value << shift);
      access_.template store<U>(loc.chunk0_byte, a);
      return;
    }

    auto const lo_bits = static_cast<unsigned>(chunk_bits - shift);
    auto const hi_bits = static_cast<unsigned>(r.bit_width - lo_bits);

    U a = access_.template load<U>(loc.chunk0_byte);
    U b = access_.template load<U>(loc.chunk0_byte + sizeof(U));

    U const lo_mask = detail::bit_mask<U>(lo_bits) << shift;
    U const hi_mask = detail::bit_mask<U>(hi_bits);

    a = (a & ~lo_mask) | ((value << shift) & lo_mask);
    b = (b & ~hi_mask) | (value >> lo_bits);

    access_.template store<U>(loc.chunk0_byte, a);
    access_.template store<U>(loc.chunk0_byte + sizeof(U), b);
  }

 private:
  detail::storage_accessor<Storage> access_;
};

template <detail::storage_element Storage>
  requires(!std::is_reference_v<Storage>)
[[nodiscard]] auto
const_bit_view(Storage const* p) noexcept -> bit_view<Storage const> {
  return bit_view<Storage const>(p);
}

} // namespace dwarfs
