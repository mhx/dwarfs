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

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <type_traits>
#include <utility>

#include <dwarfs/container/packed_value_traits.h>

namespace dwarfs::container::detail {

template <typename T>
concept field_packable = requires(
    T const& v, typename packed_value_traits<T>::encoded_type e) {
  requires integral_but_not_bool<typename packed_value_traits<T>::encoded_type>;
  {
    packed_value_traits<T>::encode(v)
  } -> std::same_as<typename packed_value_traits<T>::encoded_type>;
  { packed_value_traits<T>::decode(e) } -> std::same_as<T>;
};

template <typename T>
struct packed_field_descriptor;

// Scalar case: one logical value is one packed field.
template <field_packable T>
struct packed_field_descriptor<T> {
  using value_type = T;
  using tuple_type = std::tuple<T>;
  using size_type = std::size_t;

  static constexpr size_type field_count = 1;

  using widths_type = std::array<std::uint8_t, field_count>;

  template <size_type I>
  using field_value_type = std::tuple_element_t<I, tuple_type>;

  template <size_type I>
  using field_traits_type = packed_value_traits<field_value_type<I>>;

  template <size_type I>
  using field_encoded_type = typename field_traits_type<I>::encoded_type;

  template <size_type I>
  static constexpr auto encode_field(value_type const& value)
      noexcept(noexcept(field_traits_type<I>::encode(value)))
          -> field_encoded_type<I> {
    static_assert(I == 0);
    return field_traits_type<0>::encode(value);
  }

  template <size_type I>
  static constexpr auto decode_field(field_encoded_type<I> encoded)
      noexcept(noexcept(field_traits_type<I>::decode(encoded)))
          -> field_value_type<I> {
    static_assert(I == 0);
    return field_traits_type<0>::decode(encoded);
  }

  template <typename Writer>
  static constexpr void encode_with(value_type const& value, Writer&& write)
      noexcept(noexcept(std::forward<Writer>(write).template operator()<0>(
          encode_field<0>(value)))) {
    std::forward<Writer>(write).template operator()<0>(encode_field<0>(value));
  }

  template <typename Reader>
  static constexpr auto decode_with(Reader&& read) noexcept(noexcept(
      decode_field<0>(std::forward<Reader>(read).template operator()<0>())))
      -> value_type {
    return decode_field<0>(std::forward<Reader>(read).template operator()<0>());
  }
};

// Tuple case: one logical value is N independently packed fields.
template <field_packable... Ts>
struct packed_field_descriptor<std::tuple<Ts...>> {
  using value_type = std::tuple<Ts...>;
  using tuple_type = value_type;
  using size_type = std::size_t;

  static constexpr size_type field_count = sizeof...(Ts);

  using widths_type = std::array<std::uint8_t, field_count>;

  template <size_type I>
  using field_value_type = std::tuple_element_t<I, tuple_type>;

  template <size_type I>
  using field_traits_type = packed_value_traits<field_value_type<I>>;

  template <size_type I>
  using field_encoded_type = typename field_traits_type<I>::encoded_type;

  template <size_type I>
  static constexpr auto encode_field(value_type const& value)
      noexcept(noexcept(field_traits_type<I>::encode(std::get<I>(value))))
          -> field_encoded_type<I> {
    return field_traits_type<I>::encode(std::get<I>(value));
  }

  template <size_type I>
  static constexpr auto decode_field(field_encoded_type<I> encoded)
      noexcept(noexcept(field_traits_type<I>::decode(encoded)))
          -> field_value_type<I> {
    return field_traits_type<I>::decode(encoded);
  }

  template <typename Writer>
  static constexpr void encode_with(value_type const& value, Writer&& write)
      noexcept(noexcept_encode_with<Writer>()) {
    auto&& writer = write;
    encode_with_impl(value, writer, std::make_index_sequence<field_count>{});
  }

  template <typename Reader>
  static constexpr auto decode_with(Reader&& read)
      noexcept(noexcept_decode_with<Reader>()) -> value_type {
    auto&& reader = read;
    return decode_with_impl(reader, std::make_index_sequence<field_count>{});
  }

 private:
  template <typename Writer, size_type... I>
  static constexpr void encode_with_impl(value_type const& value, Writer& write,
                                         std::index_sequence<I...>) {
    (write.template operator()<I>(encode_field<I>(value)), ...);
  }

  template <typename Reader, size_type... I>
  static constexpr auto
  decode_with_impl(Reader& read, std::index_sequence<I...>) -> value_type {
    return value_type{
        decode_field<I>(read.template operator()<I>())...,
    };
  }

  template <typename Writer, size_type... I>
  static consteval bool noexcept_encode_with_impl(std::index_sequence<I...>) {
    return (noexcept(std::declval<Writer&>().template operator()<I>(
                std::declval<field_encoded_type<I>>())) &&
            ...);
  }

  template <typename Writer>
  static consteval bool noexcept_encode_with() {
    return noexcept_encode_with_impl<Writer>(
        std::make_index_sequence<field_count>{});
  }

  template <typename Reader, size_type... I>
  static consteval bool noexcept_decode_with_impl(std::index_sequence<I...>) {
    return (noexcept(decode_field<I>(
                std::declval<Reader&>().template operator()<I>())) &&
            ...);
  }

  template <typename Reader>
  static consteval bool noexcept_decode_with() {
    return noexcept_decode_with_impl<Reader>(
        std::make_index_sequence<field_count>{});
  }
};

} // namespace dwarfs::container::detail
