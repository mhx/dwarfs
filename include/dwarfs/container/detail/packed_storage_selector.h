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
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

#include <dwarfs/container/detail/packed_field_descriptor.h>

namespace dwarfs::container::detail {

template <typename T>
struct packed_storage_selector {
 private:
  using field_descriptor = packed_field_descriptor<T>;
  using size_type = std::size_t;

  template <size_type I>
  using field_encoded_type =
      typename field_descriptor::template field_encoded_type<I>;

  template <size_type I>
  using field_storage_type = std::make_unsigned_t<field_encoded_type<I>>;

  template <typename IndexSequence>
  struct impl;

  template <size_type... I>
  struct impl<std::index_sequence<I...>> {
    using first_storage_type = field_storage_type<0>;

    static constexpr bool uses_shared_underlying =
        (std::same_as<first_storage_type, field_storage_type<I>> && ...);

    using underlying_type =
        std::conditional_t<uses_shared_underlying, first_storage_type,
                           std::uint8_t>;
  };

  using impl_type =
      impl<std::make_index_sequence<field_descriptor::field_count>>;

 public:
  static constexpr bool uses_shared_underlying =
      impl_type::uses_shared_underlying;

  using underlying_type = typename impl_type::underlying_type;
};

} // namespace dwarfs::container::detail
