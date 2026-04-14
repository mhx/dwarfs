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

#include <compare>
#include <concepts>
#include <cstddef>
#include <type_traits>

namespace dwarfs::container::detail {

template <typename Container>
class index_based_value_proxy {
 public:
  using container_type = Container;
  using value_type = typename container_type::value_type;
  using size_type = typename container_type::size_type;

  index_based_value_proxy(container_type& vec, size_type i)
      : vec_{vec}
      , i_{i} {}

  index_based_value_proxy(index_based_value_proxy const&) = default;
  index_based_value_proxy(index_based_value_proxy&&) = default;

  operator value_type() const { return vec_.get(i_); }

  index_based_value_proxy& operator=(value_type value) {
    vec_.set(i_, value);
    return *this;
  }

  // Required for proxy-reference iterators to satisfy indirectly_writable.
  // NOLINTNEXTLINE(misc-unconventional-assign-operator,cppcoreguidelines-c-copy-assignment-signature)
  index_based_value_proxy const& operator=(value_type value) const {
    vec_.set(i_, value);
    return *this;
  }

  index_based_value_proxy& operator=(index_based_value_proxy const& other) {
    if (this != &other) {
      *this = static_cast<value_type>(other);
    }
    return *this;
  }

  friend void
  swap(index_based_value_proxy a, index_based_value_proxy b) noexcept {
    value_type tmp = a;
    a = static_cast<value_type>(b);
    b = tmp;
  }

  friend bool
  operator==(index_based_value_proxy const& lhs, value_type const& rhs)
    requires std::is_class_v<value_type>
  {
    return static_cast<value_type>(lhs) == rhs;
  }

  friend bool
  operator==(value_type const& lhs, index_based_value_proxy const& rhs)
    requires std::is_class_v<value_type>
  {
    return lhs == static_cast<value_type>(rhs);
  }

  friend bool operator==(index_based_value_proxy const& lhs,
                         index_based_value_proxy const& rhs)
    requires std::is_class_v<value_type>
  {
    return static_cast<value_type>(lhs) == static_cast<value_type>(rhs);
  }

  friend auto
  operator<=>(index_based_value_proxy const& lhs, value_type const& rhs)
      -> std::compare_three_way_result_t<value_type>
    requires std::is_class_v<value_type> &&
             std::three_way_comparable<value_type>
  {
    return static_cast<value_type>(lhs) <=> rhs;
  }

  friend auto
  operator<=>(value_type const& lhs, index_based_value_proxy const& rhs)
      -> std::compare_three_way_result_t<value_type>
    requires std::is_class_v<value_type> &&
             std::three_way_comparable<value_type>
  {
    return lhs <=> static_cast<value_type>(rhs);
  }

  friend auto operator<=>(index_based_value_proxy const& lhs,
                          index_based_value_proxy const& rhs)
      -> std::compare_three_way_result_t<value_type>
    requires std::is_class_v<value_type> &&
             std::three_way_comparable<value_type>
  {
    return static_cast<value_type>(lhs) <=> static_cast<value_type>(rhs);
  }

 private:
  container_type& vec_;
  size_type i_;
};

} // namespace dwarfs::container::detail
