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
#include <stdexcept>
#include <type_traits>

namespace dwarfs::internal {

namespace detail {

template <typename T>
struct underlying_type {
  using type = T;
};

// specialization for enums
template <typename T>
  requires std::is_enum_v<T>
struct underlying_type<T> {
  using type = std::underlying_type_t<T>;
};

} // namespace detail

template <typename T, size_t DataBits = 3, typename DataType = unsigned>
class packed_ptr {
 public:
  using data_type = DataType;
  static constexpr size_t data_bits = DataBits;
  static constexpr size_t data_mask = (1 << data_bits) - 1;

  static_assert(
      std::unsigned_integral<typename detail::underlying_type<data_type>::type>,
      "data_type must be an unsigned integer type");

  explicit packed_ptr(T* p = nullptr, data_type data = {})
      : p_(build_packed_ptr(p, data)) {}

  void set(T* p) { p_ = build_packed_ptr(p, get_data()); }
  // NOLINTNEXTLINE(performance-no-int-to-ptr)
  T* get() const { return reinterpret_cast<T*>(p_ & ~data_mask); }

  T* operator->() const { return get(); }
  T& operator*() const { return *get(); }
  T& operator[](std::ptrdiff_t i) const { return get()[i]; }

  data_type get_data() const { return static_cast<data_type>(p_ & data_mask); }
  void set_data(data_type data) { p_ = build_packed_ptr(get(), data); }

 private:
  static uintptr_t build_packed_ptr(T* p, data_type data) {
    auto value = reinterpret_cast<uintptr_t>(p);
    auto data_value = static_cast<uintptr_t>(data);
    if (value & data_mask) {
      throw std::invalid_argument("pointer is not aligned");
    }
    if (data_value & ~data_mask) {
      throw std::invalid_argument("data out of bounds");
    }
    return value | data_value;
  }

  uintptr_t p_;
};

} // namespace dwarfs::internal
