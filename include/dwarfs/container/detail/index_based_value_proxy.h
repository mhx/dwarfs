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
#include <functional>
#include <optional>
#include <tuple>
#include <type_traits>

#include <dwarfs/container/detail/concepts.h>
#include <dwarfs/container/detail/packed_field_descriptor.h>

namespace dwarfs::container::detail {

template <typename T>
struct is_std_optional : std::false_type {};

template <typename U>
struct is_std_optional<std::optional<U>> : std::true_type {};

template <typename T>
constexpr inline bool is_std_optional_v = is_std_optional<T>::value;

template <typename Container, std::size_t I>
struct proxy_field_accessor_policy {
  using container_type = Container;
  using size_type = typename Container::size_type;
  using value_type = typename packed_field_descriptor<
      typename Container::value_type>::template field_value_type<I>;

  // A single field is a leaf: it has no sub-fields of its own.
  static constexpr size_type subfield_count = 1;

  static value_type load(container_type& c, size_type i) {
    return c.template get_field<I>(i);
  }
  static void store(container_type& c, size_type i, value_type value) {
    c.template set_field<I>(i, value);
  }
};

template <typename Container>
struct proxy_value_accessor_policy {
  using container_type = Container;
  using size_type = typename Container::size_type;
  using value_type = typename Container::value_type;

  static constexpr size_type subfield_count =
      packed_field_descriptor<value_type>::field_count;

  template <size_type I>
  using subfield_accessor = proxy_field_accessor_policy<Container, I>;

  static value_type load(container_type& c, size_type i) { return c.get(i); }
  static void store(container_type& c, size_type i, value_type value) {
    c.set(i, value);
  }
};

template <typename AccessorPolicy>
class basic_index_based_proxy {
 public:
  using container_type = typename AccessorPolicy::container_type;
  using size_type = typename AccessorPolicy::size_type;
  using value_type = typename AccessorPolicy::value_type;

  basic_index_based_proxy(container_type& vec, size_type i) noexcept
      : vec_{vec}
      , i_{i} {}

  basic_index_based_proxy(basic_index_based_proxy const&) noexcept = default;
  basic_index_based_proxy(basic_index_based_proxy&&) noexcept = default;

  operator value_type() const { return AccessorPolicy::load(vec_, i_); }
  [[nodiscard]] auto load() const -> value_type {
    return AccessorPolicy::load(vec_, i_);
  }
  [[nodiscard]] auto operator+() const -> value_type { return load(); }

  basic_index_based_proxy& operator=(value_type value) {
    AccessorPolicy::store(vec_, i_, value);
    return *this;
  }

  // Required for proxy-reference style semantics.
  // NOLINTNEXTLINE(misc-unconventional-assign-operator,cppcoreguidelines-c-copy-assignment-signature)
  basic_index_based_proxy const& operator=(value_type value) const {
    AccessorPolicy::store(vec_, i_, value);
    return *this;
  }

  basic_index_based_proxy& operator=(basic_index_based_proxy const& other) {
    if (this != &other) {
      *this = other.load();
    }
    return *this;
  }

  friend void
  swap(basic_index_based_proxy a, basic_index_based_proxy b) noexcept {
    value_type tmp = a.load();
    a = b.load();
    b = tmp;
  }

  // tuple-like access (only for proxies that actually have sub-fields)

  template <size_type I>
  [[nodiscard]] auto get() const
    requires(AccessorPolicy::subfield_count > 1)
  {
    static_assert(I < AccessorPolicy::subfield_count);
    return basic_index_based_proxy<
        typename AccessorPolicy::template subfield_accessor<I>>{vec_, i_};
  }

  template <size_type I>
  friend auto get(basic_index_based_proxy const& proxy)
    requires(AccessorPolicy::subfield_count > 1)
  {
    return proxy.template get<I>();
  }

  // arithmetic operators support

  template <typename Rhs>
    requires closed_under<std::plus<>, value_type, Rhs>
  basic_index_based_proxy& operator+=(Rhs&& rhs) {
    *this = std::plus<>{}(load(), std::forward<Rhs>(rhs));
    return *this;
  }

  template <typename Rhs>
    requires closed_under<std::minus<>, value_type, Rhs>
  basic_index_based_proxy& operator-=(Rhs&& rhs) {
    *this = std::minus<>{}(load(), std::forward<Rhs>(rhs));
    return *this;
  }

  basic_index_based_proxy& operator++()
    requires closed_under<std::plus<>, value_type, int>
  {
    *this += 1;
    return *this;
  }

  value_type operator++(int)
    requires closed_under<std::plus<>, value_type, int>
  {
    value_type old = load();
    ++*this;
    return old;
  }

  basic_index_based_proxy& operator--()
    requires closed_under<std::minus<>, value_type, int>
  {
    *this -= 1;
    return *this;
  }

  value_type operator--(int)
    requires closed_under<std::minus<>, value_type, int>
  {
    value_type old = load();
    --*this;
    return old;
  }

  // optional-like interface for optional fields

  bool has_value() const
    requires is_std_optional_v<value_type>
  {
    return static_cast<value_type>(*this).has_value();
  }

  auto value() const
    requires is_std_optional_v<value_type>
  {
    return static_cast<value_type>(*this).value();
  }

  auto operator*() const
    requires is_std_optional_v<value_type>
  {
    return value();
  }

  friend bool
  operator==(basic_index_based_proxy const& lhs, value_type const& rhs)
    requires std::is_class_v<value_type>
  {
    return static_cast<value_type>(lhs) == rhs;
  }

  friend bool
  operator==(value_type const& lhs, basic_index_based_proxy const& rhs)
    requires std::is_class_v<value_type>
  {
    return lhs == static_cast<value_type>(rhs);
  }

  friend bool operator==(basic_index_based_proxy const& lhs,
                         basic_index_based_proxy const& rhs)
    requires std::is_class_v<value_type>
  {
    return static_cast<value_type>(lhs) == static_cast<value_type>(rhs);
  }

  friend auto
  operator<=>(basic_index_based_proxy const& lhs, value_type const& rhs)
      -> std::compare_three_way_result_t<value_type>
    requires std::is_class_v<value_type> &&
             std::three_way_comparable<value_type>
  {
    return static_cast<value_type>(lhs) <=> rhs;
  }

  friend auto
  operator<=>(value_type const& lhs, basic_index_based_proxy const& rhs)
      -> std::compare_three_way_result_t<value_type>
    requires std::is_class_v<value_type> &&
             std::three_way_comparable<value_type>
  {
    return lhs <=> static_cast<value_type>(rhs);
  }

  friend auto operator<=>(basic_index_based_proxy const& lhs,
                          basic_index_based_proxy const& rhs)
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

template <typename Container, std::size_t I>
using index_based_field_proxy =
    basic_index_based_proxy<proxy_field_accessor_policy<Container, I>>;

template <typename Container>
using index_based_value_proxy =
    basic_index_based_proxy<proxy_value_accessor_policy<Container>>;

} // namespace dwarfs::container::detail
