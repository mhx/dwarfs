/* vim:set ts=2 sw=2 sts=2 et: */
/**
 * \author     Marcus Holland-Moritz (github@mhxnet.de)
 * \copyright  Copyright (c) Marcus Holland-Moritz
 *
 * This file is part of dwarfs.
 *
 * dwarfs is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * dwarfs is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with dwarfs.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <compare>
#include <concepts>
#include <memory>
#include <span>
#include <vector>

namespace dwarfs {

namespace detail {

template <typename T>
concept byte_range = requires(T const& t) {
  { t.data() } -> std::same_as<uint8_t const*>;
  { t.size() } -> std::same_as<size_t>;
};

std::strong_ordering
compare_spans(std::span<uint8_t const> lhs, std::span<uint8_t const> rhs);

} // namespace detail

class byte_buffer_interface {
 public:
  virtual ~byte_buffer_interface() = default;

  virtual uint8_t const* data() const = 0;
  virtual size_t size() const = 0;
  virtual std::span<uint8_t const> span() const = 0;
};

class mutable_byte_buffer_interface : public byte_buffer_interface {
 public:
  virtual uint8_t* mutable_data() = 0;
  virtual std::span<uint8_t> mutable_span() = 0;
  virtual void clear() = 0;
  virtual void reserve(size_t size) = 0;
  virtual void resize(size_t size) = 0;
  virtual void shrink_to_fit() = 0;

  // TODO: See if we can do without this. This will *only* be implemented
  //       in the vector_byte_buffer, other implementations will throw.
  virtual std::vector<uint8_t>& raw_vector() = 0;
};

class shared_byte_buffer {
 public:
  using value_type = uint8_t;

  shared_byte_buffer() = default;

  explicit shared_byte_buffer(std::shared_ptr<byte_buffer_interface const> bb)
      : bb_{std::move(bb)} {}

  uint8_t const* data() const { return bb_->data(); }

  size_t size() const { return bb_->size(); }

  bool empty() const { return bb_->size() == 0; }

  std::span<uint8_t const> span() const { return bb_->span(); }

  void swap(shared_byte_buffer& other) noexcept { std::swap(bb_, other.bb_); }

  template <detail::byte_range T>
  friend bool operator==(shared_byte_buffer const& lhs, T const& rhs) {
    return detail::compare_spans(lhs.span(), {rhs.data(), rhs.size()}) ==
           std::strong_ordering::equal;
  }

  template <detail::byte_range T>
    requires(!std::same_as<T, shared_byte_buffer>)
  friend bool operator==(T const& lhs, shared_byte_buffer const& rhs) {
    return detail::compare_spans({lhs.data(), lhs.size()}, rhs.span()) ==
           std::strong_ordering::equal;
  }

  template <detail::byte_range T>
  friend std::strong_ordering
  operator<=>(shared_byte_buffer const& lhs, T const& rhs) {
    return detail::compare_spans(lhs.span(), {rhs.data(), rhs.size()});
  }

  template <detail::byte_range T>
    requires(!std::same_as<T, shared_byte_buffer>)
  friend std::strong_ordering
  operator<=>(T const& lhs, shared_byte_buffer const& rhs) {
    return detail::compare_spans({lhs.data(), lhs.size()}, rhs.span());
  }

 private:
  std::shared_ptr<byte_buffer_interface const> bb_;
};

class mutable_byte_buffer {
 public:
  using value_type = uint8_t;

  explicit mutable_byte_buffer(
      std::shared_ptr<mutable_byte_buffer_interface> bb)
      : bb_{std::move(bb)} {}

  uint8_t const* data() const { return bb_->data(); }

  uint8_t* data() { return bb_->mutable_data(); }

  size_t size() const { return bb_->size(); }

  bool empty() const { return bb_->size() == 0; }

  std::span<uint8_t const> span() const { return bb_->span(); }

  std::span<uint8_t> span() { return bb_->mutable_span(); }

  void clear() { bb_->clear(); }

  void reserve(size_t size) { bb_->reserve(size); }

  void resize(size_t size) { bb_->resize(size); }

  void shrink_to_fit() { bb_->shrink_to_fit(); }

  std::vector<uint8_t>& raw_vector() { return bb_->raw_vector(); }

  void swap(mutable_byte_buffer& other) noexcept { std::swap(bb_, other.bb_); }

  template <detail::byte_range T>
  friend bool operator==(mutable_byte_buffer const& lhs, T const& rhs) {
    return detail::compare_spans(lhs.span(), {rhs.data(), rhs.size()}) ==
           std::strong_ordering::equal;
  }

  template <detail::byte_range T>
    requires(!std::same_as<T, mutable_byte_buffer>)
  friend bool operator==(T const& lhs, mutable_byte_buffer const& rhs) {
    return detail::compare_spans({lhs.data(), lhs.size()}, rhs.span()) ==
           std::strong_ordering::equal;
  }

  template <detail::byte_range T>
  friend std::strong_ordering
  operator<=>(mutable_byte_buffer const& lhs, T const& rhs) {
    return detail::compare_spans(lhs.span(), {rhs.data(), rhs.size()});
  }

  template <detail::byte_range T>
    requires(!std::same_as<T, mutable_byte_buffer>)
  friend std::strong_ordering
  operator<=>(T const& lhs, mutable_byte_buffer const& rhs) {
    return detail::compare_spans({lhs.data(), lhs.size()}, rhs.span());
  }

  auto share() const { return shared_byte_buffer{bb_}; }

 private:
  std::shared_ptr<mutable_byte_buffer_interface> bb_;
};

} // namespace dwarfs
