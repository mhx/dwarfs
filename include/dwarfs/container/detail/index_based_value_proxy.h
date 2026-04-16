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
#include <tuple>
#include <type_traits>

#include <dwarfs/container/detail/packed_field_descriptor.h>

namespace dwarfs::container::detail {

template <typename T>
struct is_std_optional : std::false_type {};

template <typename U>
struct is_std_optional<std::optional<U>> : std::true_type {};

template <typename T>
constexpr inline bool is_std_optional_v = is_std_optional<T>::value;

template <typename Container, std::size_t I>
class index_based_field_proxy {
 public:
  using container_type = Container;
  using size_type = typename container_type::size_type;
  using value_type = typename packed_field_descriptor<
      typename container_type::value_type>::template field_value_type<I>;

  index_based_field_proxy(container_type& vec, size_type i)
      : vec_{vec}
      , i_{i} {}

  index_based_field_proxy(index_based_field_proxy const&) = default;
  index_based_field_proxy(index_based_field_proxy&&) = default;

  operator value_type() const { return vec_.template get_field<I>(i_); }
  [[nodiscard]] auto load() const -> value_type {
    return static_cast<value_type>(*this);
  }
  [[nodiscard]] auto operator+() const -> value_type { return load(); }

  index_based_field_proxy& operator=(value_type value) {
    vec_.template set_field<I>(i_, value);
    return *this;
  }

  // Required for proxy-reference style semantics.
  // NOLINTNEXTLINE(misc-unconventional-assign-operator,cppcoreguidelines-c-copy-assignment-signature)
  index_based_field_proxy const& operator=(value_type value) const {
    vec_.template set_field<I>(i_, value);
    return *this;
  }

  index_based_field_proxy& operator=(index_based_field_proxy const& other) {
    if (this != &other) {
      *this = static_cast<value_type>(other);
    }
    return *this;
  }

  friend void
  swap(index_based_field_proxy a, index_based_field_proxy b) noexcept {
    value_type tmp = a;
    a = static_cast<value_type>(b);
    b = tmp;
  }

  // arithmetic operators support

  template <typename Rhs>
  index_based_field_proxy& operator+=(Rhs&& rhs)
    requires requires(value_type lhs, Rhs&& r) {
      { lhs + std::forward<Rhs>(r) } -> std::convertible_to<value_type>;
    }
  {
    *this = static_cast<value_type>(*this) + std::forward<Rhs>(rhs);
    return *this;
  }

  template <typename Rhs>
  index_based_field_proxy& operator-=(Rhs&& rhs)
    requires requires(value_type lhs, Rhs&& r) {
      { lhs - std::forward<Rhs>(r) } -> std::convertible_to<value_type>;
    }
  {
    *this = static_cast<value_type>(*this) - std::forward<Rhs>(rhs);
    return *this;
  }

  index_based_field_proxy& operator++()
    requires requires(value_type lhs) {
      { lhs + 1 } -> std::convertible_to<value_type>;
    }
  {
    *this += 1;
    return *this;
  }

  value_type operator++(int)
    requires requires(value_type lhs) {
      { lhs + 1 } -> std::convertible_to<value_type>;
    }
  {
    value_type old = *this;
    ++*this;
    return old;
  }

  index_based_field_proxy& operator--()
    requires requires(value_type lhs) {
      { lhs - 1 } -> std::convertible_to<value_type>;
    }
  {
    *this -= 1;
    return *this;
  }

  value_type operator--(int)
    requires requires(value_type lhs) {
      { lhs - 1 } -> std::convertible_to<value_type>;
    }
  {
    value_type old = *this;
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
  operator==(index_based_field_proxy const& lhs, value_type const& rhs)
    requires std::is_class_v<value_type>
  {
    return static_cast<value_type>(lhs) == rhs;
  }

  friend bool
  operator==(value_type const& lhs, index_based_field_proxy const& rhs)
    requires std::is_class_v<value_type>
  {
    return lhs == static_cast<value_type>(rhs);
  }

  friend bool operator==(index_based_field_proxy const& lhs,
                         index_based_field_proxy const& rhs)
    requires std::is_class_v<value_type>
  {
    return static_cast<value_type>(lhs) == static_cast<value_type>(rhs);
  }

  friend auto
  operator<=>(index_based_field_proxy const& lhs, value_type const& rhs)
      -> std::compare_three_way_result_t<value_type>
    requires std::is_class_v<value_type> &&
             std::three_way_comparable<value_type>
  {
    return static_cast<value_type>(lhs) <=> rhs;
  }

  friend auto
  operator<=>(value_type const& lhs, index_based_field_proxy const& rhs)
      -> std::compare_three_way_result_t<value_type>
    requires std::is_class_v<value_type> &&
             std::three_way_comparable<value_type>
  {
    return lhs <=> static_cast<value_type>(rhs);
  }

  friend auto operator<=>(index_based_field_proxy const& lhs,
                          index_based_field_proxy const& rhs)
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

template <typename Container>
class index_based_value_proxy {
 public:
  using container_type = Container;
  using value_type = typename container_type::value_type;
  using size_type = typename container_type::size_type;

 private:
  using field_descriptor = packed_field_descriptor<value_type>;
  static constexpr size_type field_count = field_descriptor::field_count;

 public:
  index_based_value_proxy(container_type& vec, size_type i)
      : vec_{vec}
      , i_{i} {}

  index_based_value_proxy(index_based_value_proxy const&) = default;
  index_based_value_proxy(index_based_value_proxy&&) = default;

  operator value_type() const { return vec_.get(i_); }
  [[nodiscard]] auto load() const -> value_type {
    return static_cast<value_type>(*this);
  }
  [[nodiscard]] auto operator+() const -> value_type { return load(); }

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

  template <size_type I>
  [[nodiscard]] auto get() const
    requires(field_count > 1)
  {
    static_assert(I < field_count);
    return index_based_field_proxy<container_type, I>{vec_, i_};
  }

  // arithmetic operators support

  template <typename Rhs>
  index_based_value_proxy& operator+=(Rhs&& rhs)
    requires requires(value_type lhs, Rhs&& r) {
      { lhs + std::forward<Rhs>(r) } -> std::convertible_to<value_type>;
    }
  {
    *this = static_cast<value_type>(*this) + std::forward<Rhs>(rhs);
    return *this;
  }

  template <typename Rhs>
  index_based_value_proxy& operator-=(Rhs&& rhs)
    requires requires(value_type lhs, Rhs&& r) {
      { lhs - std::forward<Rhs>(r) } -> std::convertible_to<value_type>;
    }
  {
    *this = static_cast<value_type>(*this) - std::forward<Rhs>(rhs);
    return *this;
  }

  index_based_value_proxy& operator++()
    requires requires(value_type lhs) {
      { lhs + 1 } -> std::convertible_to<value_type>;
    }
  {
    *this += 1;
    return *this;
  }

  value_type operator++(int)
    requires requires(value_type lhs) {
      { lhs + 1 } -> std::convertible_to<value_type>;
    }
  {
    value_type old = *this;
    ++*this;
    return old;
  }

  index_based_value_proxy& operator--()
    requires requires(value_type lhs) {
      { lhs - 1 } -> std::convertible_to<value_type>;
    }
  {
    *this -= 1;
    return *this;
  }

  value_type operator--(int)
    requires requires(value_type lhs) {
      { lhs - 1 } -> std::convertible_to<value_type>;
    }
  {
    value_type old = *this;
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

  template <size_type I>
  friend auto get(index_based_value_proxy const& proxy)
    requires(field_count > 1)
  {
    return proxy.template get<I>();
  }

 private:
  container_type& vec_;
  size_type i_;
};

} // namespace dwarfs::container::detail
