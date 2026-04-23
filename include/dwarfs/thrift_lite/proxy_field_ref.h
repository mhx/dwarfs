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
#include <type_traits>
#include <utility>

#include <dwarfs/thrift_lite/detail/concepts.h>

namespace dwarfs::thrift_lite {

template <typename Proxy>
class proxy_field_ref {
 public:
  using proxy_type = Proxy;
  using value_type = typename proxy_type::value_type;

  explicit proxy_field_ref(proxy_type proxy)
      : proxy_(std::move(proxy)) {}

  auto has_value() const noexcept -> bool { return true; }

  auto value() const -> value_type { return proxy_.load(); }
  auto operator*() const -> value_type { return value(); }
  auto load() const -> value_type { return value(); }

  auto operator=(value_type const& v) -> proxy_field_ref&
    requires requires(proxy_type p, value_type const& x) {
      { p = x } -> std::same_as<proxy_type&>;
    }
  {
    proxy_ = v;
    return *this;
  }

  template <typename U>
  friend bool operator==(proxy_field_ref const& lhs, U const& rhs) {
    return lhs.value() == rhs;
  }

  template <typename U>
  friend auto operator<=>(proxy_field_ref const& lhs, U const& rhs) {
    return lhs.value() <=> rhs;
  }

  template <typename U>
  friend bool operator==(U const& lhs, proxy_field_ref const& rhs) {
    return lhs == rhs.value();
  }

  template <typename U>
  friend auto operator<=>(U const& lhs, proxy_field_ref const& rhs) {
    return lhs <=> rhs.value();
  }

  auto operator+=(value_type const& v) -> proxy_field_ref&
    requires(detail::arithmetic_type<value_type> &&
             requires(proxy_type p, value_type const& x) {
               { p = x } -> std::same_as<proxy_type&>;
             })
  {
    proxy_ = value() + v;
    return *this;
  }

  auto operator-=(value_type const& v) -> proxy_field_ref&
    requires(detail::arithmetic_type<value_type> &&
             requires(proxy_type p, value_type const& x) {
               { p = x } -> std::same_as<proxy_type&>;
             })
  {
    proxy_ = value() - v;
    return *this;
  }

  auto operator++() -> proxy_field_ref&
    requires(detail::arithmetic_type<value_type> &&
             requires(proxy_type p, value_type const& x) {
               { p = x } -> std::same_as<proxy_type&>;
             })
  {
    proxy_ = value() + value_type{1};
    return *this;
  }

  auto operator++(int) -> value_type
    requires(detail::arithmetic_type<value_type> &&
             requires(proxy_type p, value_type const& x) {
               { p = x } -> std::same_as<proxy_type&>;
             })
  {
    auto old = value();
    ++(*this);
    return old;
  }

  auto operator--() -> proxy_field_ref&
    requires(detail::arithmetic_type<value_type> &&
             requires(proxy_type p, value_type const& x) {
               { p = x } -> std::same_as<proxy_type&>;
             })
  {
    proxy_ = value() - value_type{1};
    return *this;
  }

  auto operator--(int) -> value_type
    requires(detail::arithmetic_type<value_type> &&
             requires(proxy_type p, value_type const& x) {
               { p = x } -> std::same_as<proxy_type&>;
             })
  {
    auto old = value();
    --(*this);
    return old;
  }

 private:
  proxy_type proxy_;
};

} // namespace dwarfs::thrift_lite
