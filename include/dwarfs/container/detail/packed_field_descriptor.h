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

template <typename Value, field_packable... Ts>
struct packed_field_descriptor_base {
  using value_type = Value;
  using tuple_type = std::tuple<Ts...>;
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
      noexcept(noexcept(field_traits_type<I>::encode(value)))
          -> field_encoded_type<I>
    requires(field_count == 1)
  {
    static_assert(I == 0);
    return field_traits_type<0>::encode(value);
  }

  template <size_type I>
  static constexpr auto encode_field(value_type const& value)
      noexcept(noexcept(field_traits_type<I>::encode(std::get<I>(value))))
          -> field_encoded_type<I>
    requires(field_count > 1)
  {
    static_assert(I < field_count);
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
    auto&& writer = std::forward<Writer>(write);
    [&]<size_type... I>(std::index_sequence<I...>) {
      (writer.template operator()<I>(encode_field<I>(value)), ...);
    }(std::make_index_sequence<field_count>{});
  }

  template <typename Reader>
  static constexpr auto decode_with(Reader&& read)
      noexcept(noexcept_decode_with<Reader>()) -> value_type {
    auto&& reader = std::forward<Reader>(read);
    return [&]<size_type... I>(std::index_sequence<I...>) {
      return value_type{
          decode_field<I>(reader.template operator()<I>())...,
      };
    }(std::make_index_sequence<field_count>{});
  }

 private:
  template <typename Writer>
  static consteval bool noexcept_encode_with() {
    return []<std::size_t... I>(std::index_sequence<I...>) {
      return (noexcept(std::declval<Writer&>().template operator()<I>(
                  std::declval<field_encoded_type<I>>())) &&
              ...);
    }(std::make_index_sequence<field_count>{});
  }

  template <typename Reader>
  static consteval bool noexcept_decode_with() {
    return []<std::size_t... I>(std::index_sequence<I...>) {
      return (noexcept(decode_field<I>(
                  std::declval<Reader&>().template operator()<I>())) &&
              ...);
    }(std::make_index_sequence<field_count>{});
  }
};

template <typename T>
struct packed_field_descriptor;

// Scalar case: one logical value is one packed field.
template <field_packable T>
struct packed_field_descriptor<T> final : packed_field_descriptor_base<T, T> {};

// Tuple case: one logical value is N independently packed fields.
template <field_packable... Ts>
struct packed_field_descriptor<std::tuple<Ts...>> final
    : packed_field_descriptor_base<std::tuple<Ts...>, Ts...> {};

} // namespace dwarfs::container::detail
