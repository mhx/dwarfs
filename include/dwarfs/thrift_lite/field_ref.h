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
#include <optional>
#include <type_traits>
#include <utility>

namespace dwarfs::thrift_lite {

namespace detail {

template <typename Ref>
using pointee_t = std::remove_reference_t<Ref>;

template <typename Ref>
constexpr inline bool is_mutable_ref_v = !std::is_const_v<pointee_t<Ref>>;

template <typename BitRef>
concept bool_ref = requires(BitRef b) {
  { static_cast<bool>(b) } -> std::same_as<bool>;
};

template <typename BitRef>
concept mutable_bool_ref = bool_ref<BitRef> && requires(BitRef b) {
  { b = false } -> std::same_as<BitRef&>;
  { b = true } -> std::same_as<BitRef&>;
};

} // namespace detail

template <typename Ref>
  requires(std::is_reference_v<Ref>)
class field_ref {
 public:
  static constexpr bool is_mutable = detail::is_mutable_ref_v<Ref>;
  using value_type = std::remove_cv_t<detail::pointee_t<Ref>>;

  explicit field_ref(Ref ref) noexcept
    requires(!std::is_rvalue_reference_v<Ref>)
      : ptr_{&ref} {}

  explicit field_ref(value_type& ref) noexcept
    requires(std::is_rvalue_reference_v<Ref>)
      : ptr_{&ref} {}

  field_ref& operator=(value_type const& v)
      noexcept(std::is_nothrow_copy_assignable_v<value_type>)
    requires is_mutable
  {
    *ptr_ = v;
    return *this;
  }

  field_ref& operator=(value_type&& v)
      noexcept(std::is_nothrow_move_assignable_v<value_type>)
    requires is_mutable
  {
    *ptr_ = std::move(v);
    return *this;
  }

  template <typename U>
  void copy_from(field_ref<U> const& other)
      noexcept(std::is_nothrow_assignable_v<Ref, U>)
    requires(is_mutable && std::assignable_from<Ref, U>)
  {
    *ptr_ = other.value();
  }

  template <typename U>
  friend bool operator==(field_ref const& lhs, field_ref<U> const& rhs) {
    return lhs.value() == rhs.value();
  }

  template <typename U>
  friend auto operator<=>(field_ref const& lhs, field_ref<U> const& rhs) {
    return lhs.value() <=> rhs.value();
  }

  template <typename U>
  friend bool operator==(field_ref const& lhs, U const& rhs) {
    return lhs.value() == rhs;
  }

  template <typename U>
  friend auto operator<=>(field_ref const& lhs, U const& rhs) {
    return lhs.value() <=> rhs;
  }

  template <typename U>
  friend bool operator==(U const& lhs, field_ref const& rhs) {
    return rhs == lhs;
  }

  template <typename U>
  friend auto operator<=>(U const& lhs, field_ref const& rhs) {
    return rhs <=> lhs;
  }

  auto has_value() const noexcept -> bool { return true; }

  auto value() const noexcept -> Ref { return static_cast<Ref>(*ptr_); }

  auto operator*() const noexcept -> Ref { return value(); }

  auto
  operator->() const noexcept -> std::add_pointer_t<detail::pointee_t<Ref>> {
    return ptr_;
  }

  template <typename U>
    requires(is_mutable && !std::is_array_v<std::remove_reference_t<U>> &&
             std::assignable_from<detail::pointee_t<Ref>&, U>)
  auto operator=(U&& v) -> field_ref& {
    *ptr_ = std::forward<U>(v);
    return *this;
  }

  template <typename U>
    requires(is_mutable && std::is_array_v<std::remove_reference_t<U>> &&
             std::assignable_from<detail::pointee_t<Ref>&,
                                  decltype(std::data(std::declval<U>()))>)
  auto operator=(U&& v) -> field_ref& {
    *ptr_ = std::data(std::forward<U>(v));
    return *this;
  }

  void reset() noexcept
    requires is_mutable
  {
    *ptr_ = value_type{};
  }

  auto ensure() noexcept -> Ref
    requires is_mutable
  {
    return value();
  }

  template <typename I>
  auto operator[](I&& i) const -> decltype(auto) {
    return value()[std::forward<I>(i)];
  }

 private:
  std::add_pointer_t<detail::pointee_t<Ref>> ptr_;
};

template <typename Ref, typename BitRef>
  requires(std::is_reference_v<Ref> && detail::bool_ref<BitRef>)
class optional_field_ref {
 public:
  static constexpr bool is_mutable =
      detail::is_mutable_ref_v<Ref> && detail::mutable_bool_ref<BitRef>;
  using value_type = std::remove_cv_t<detail::pointee_t<Ref>>;

  optional_field_ref(Ref ref, BitRef const& is_set) noexcept
    requires(!std::is_rvalue_reference_v<Ref>)
      : ptr_{&ref}
      , is_set_{is_set} {}

  optional_field_ref(value_type& ref, BitRef const& is_set) noexcept
    requires(std::is_rvalue_reference_v<Ref>)
      : ptr_{&ref}
      , is_set_{is_set} {}

  optional_field_ref& operator=(value_type const& v)
      noexcept(std::is_nothrow_copy_assignable_v<value_type>)
    requires is_mutable
  {
    *ptr_ = v;
    is_set_ = true;
    return *this;
  }

  optional_field_ref& operator=(value_type&& v)
      noexcept(std::is_nothrow_move_assignable_v<value_type>)
    requires is_mutable
  {
    *ptr_ = std::move(v);
    is_set_ = true;
    return *this;
  }

  template <typename U, typename V>
  void copy_from(optional_field_ref<U, V> const& other)
      noexcept(std::is_nothrow_assignable_v<Ref, U>)
    requires is_mutable
  {
    bool const other_has_value = other.has_value();
    if (other_has_value) {
      *ptr_ = other.value();
    }
    is_set_ = other_has_value;
  }

  template <typename U, typename V>
  friend bool operator==(optional_field_ref const& lhs,
                         optional_field_ref<U, V> const& rhs) {
    if (lhs.has_value() && rhs.has_value()) {
      return lhs.value() == rhs.value();
    }
    return lhs.has_value() == rhs.has_value();
  }

  template <typename U, typename V>
  friend auto operator<=>(optional_field_ref const& lhs,
                          optional_field_ref<U, V> const& rhs) {
    if (lhs.has_value() && rhs.has_value()) {
      return lhs.value() <=> rhs.value();
    }
    return lhs.has_value() <=> rhs.has_value();
  }

  template <typename U>
  friend bool operator==(optional_field_ref const& lhs, U const& rhs) {
    return lhs.has_value() && lhs.value() == rhs;
  }

  template <typename U>
  friend auto operator<=>(optional_field_ref const& lhs, U const& rhs) {
    if (!lhs.has_value()) {
      return std::strong_ordering::less;
    }
    return lhs.value() <=> rhs;
  }

  template <typename U>
  friend bool operator==(U const& lhs, optional_field_ref const& rhs) {
    return rhs == lhs;
  }

  template <typename U>
  friend auto operator<=>(U const& lhs, optional_field_ref const& rhs) {
    return rhs <=> lhs;
  }

  auto has_value() const noexcept -> bool { return static_cast<bool>(is_set_); }

  explicit operator bool() const noexcept { return has_value(); }

  auto value() const -> Ref {
    if (!has_value()) {
      throw std::bad_optional_access{};
    }
    return static_cast<Ref>(*ptr_);
  }

  auto operator*() const -> Ref { return value(); }

  auto operator->() const -> std::add_pointer_t<detail::pointee_t<Ref>> {
    return &value();
  }

  template <typename U = value_type>
    requires(std::copy_constructible<value_type> &&
             !std::is_array_v<std::remove_reference_t<U>> &&
             std::constructible_from<value_type, U>)
  auto value_or(U&& default_value) const -> value_type {
    if (has_value()) {
      return value_type(static_cast<Ref>(*ptr_));
    }
    return value_type(std::forward<U>(default_value));
  }

  template <typename U = value_type>
    requires(std::copy_constructible<value_type> &&
             std::is_array_v<std::remove_reference_t<U>> &&
             std::constructible_from<value_type,
                                     decltype(std::data(std::declval<U>()))>)
  auto value_or(U&& default_value) const -> value_type {
    if (has_value()) {
      return value_type(static_cast<Ref>(*ptr_));
    }
    return value_type(std::data(std::forward<U>(default_value)));
  }

  template <typename U = value_type>
    requires(std::copy_constructible<value_type>)
  auto to_optional() const -> std::optional<value_type> {
    if (!has_value()) {
      return std::nullopt;
    }
    return std::optional<value_type>(value_type(static_cast<Ref>(*ptr_)));
  }

  template <typename... Args>
    requires(is_mutable &&
             (sizeof...(Args) != 1 ||
              !std::is_array_v<std::remove_reference_t<
                  std::tuple_element_t<0, std::tuple<Args...>>>>) &&
             std::constructible_from<value_type, Args...> &&
             std::assignable_from<detail::pointee_t<Ref>&, value_type>)
  auto emplace(Args&&... args) -> detail::pointee_t<Ref>& {
    is_set_ = true;
    *ptr_ = value_type(std::forward<Args>(args)...);
    return *ptr_;
  }

  template <typename U>
    requires(is_mutable && std::is_array_v<std::remove_reference_t<U>> &&
             std::constructible_from<value_type,
                                     decltype(std::data(std::declval<U>()))>)
  auto emplace(U&& v) -> detail::pointee_t<Ref>& {
    is_set_ = true;
    *ptr_ = value_type(std::data(std::forward<U>(v)));
    return *ptr_;
  }

  template <typename U>
    requires(is_mutable && !std::is_array_v<std::remove_reference_t<U>> &&
             std::assignable_from<detail::pointee_t<Ref>&, U>)
  auto operator=(U&& v) -> optional_field_ref& {
    is_set_ = true;
    *ptr_ = std::forward<U>(v);
    return *this;
  }

  template <typename U>
    requires(is_mutable && std::is_array_v<std::remove_reference_t<U>> &&
             std::assignable_from<detail::pointee_t<Ref>&,
                                  decltype(std::data(std::declval<U>()))>)
  auto operator=(U&& v) -> optional_field_ref& {
    is_set_ = true;
    *ptr_ = std::data(std::forward<U>(v));
    return *this;
  }

  void reset() noexcept
    requires is_mutable
  {
    *ptr_ = value_type{};
    is_set_ = false;
  }

  auto ensure() -> Ref
    requires is_mutable
  {
    if (!has_value()) {
      emplace();
    }
    return value();
  }

 private:
  std::add_pointer_t<detail::pointee_t<Ref>> ptr_;
  BitRef is_set_;
};

} // namespace dwarfs::thrift_lite
