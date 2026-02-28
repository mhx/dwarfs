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

#include <concepts>

#include <dwarfs/thrift_lite/detail/concepts.h>
#include <dwarfs/thrift_lite/detail/type_class.h>

#include <dwarfs/thrift_lite/traits.h>
#include <dwarfs/thrift_lite/types.h>

namespace dwarfs::thrift_lite::detail {

namespace detail {

template <typename TypeClass>
struct protocol_type {};

template <>
struct protocol_type<type_class::integral> {
  template <std::integral T>
  static consteval auto get_ttype() -> ttype {
    if constexpr (std::is_same_v<T, bool>) {
      return ttype::bool_t;
    } else if constexpr (sizeof(T) == 1) {
      return ttype::byte_t;
    } else if constexpr (sizeof(T) == 2) {
      return ttype::i16_t;
    } else if constexpr (sizeof(T) == 4) {
      return ttype::i32_t;
    } else if constexpr (sizeof(T) == 8) {
      return ttype::i64_t;
    } else {
      static_assert(always_false_v<T>, "unsupported integral type");
    }
  }
};

template <>
struct protocol_type<type_class::enumeration> {
  template <enumeration_type T>
  static consteval auto get_ttype() -> ttype {
    return ttype::i32_t;
  }
};

template <>
struct protocol_type<type_class::binary> {
  template <typename>
  static consteval auto get_ttype() -> ttype {
    return ttype::binary_t;
  }
};

template <>
struct protocol_type<type_class::string> {
  template <typename>
  static consteval auto get_ttype() -> ttype {
    return ttype::binary_t;
  }
};

template <>
struct protocol_type<type_class::structure> {
  template <typename>
  static consteval auto get_ttype() -> ttype {
    return ttype::struct_t;
  }
};

template <typename ValueTypeClass>
struct protocol_type<type_class::list<ValueTypeClass>> {
  template <typename>
  static consteval auto get_ttype() -> ttype {
    return ttype::list_t;
  }
};

template <typename ValueTypeClass>
struct protocol_type<type_class::set<ValueTypeClass>> {
  template <typename>
  static consteval auto get_ttype() -> ttype {
    return ttype::set_t;
  }
};

template <typename KeyTypeClass, typename MappedTypeClass>
struct protocol_type<type_class::map<KeyTypeClass, MappedTypeClass>> {
  template <typename>
  static consteval auto get_ttype() -> ttype {
    return ttype::map_t;
  }
};

} // namespace detail

template <typename TypeClass, typename T>
constexpr inline auto ttype_v =
    detail::protocol_type<TypeClass>::template get_ttype<T>();

} // namespace dwarfs::thrift_lite::detail
