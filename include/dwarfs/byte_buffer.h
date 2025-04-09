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

namespace internal {

class malloc_buffer;

}

class byte_buffer_interface {
 public:
  virtual ~byte_buffer_interface() = default;

  virtual uint8_t const* data() const = 0;
  virtual size_t size() const = 0;
  virtual size_t capacity() const = 0;
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

  // Freezes the location of the buffer in memory, i.e. all further calls
  // that would reallocate the buffer will throw.
  virtual void freeze_location() = 0;

  virtual void append(void const* data, size_t size) = 0;

  virtual internal::malloc_buffer& raw_buffer() = 0;
};

class shared_byte_buffer {
 public:
  using value_type = uint8_t;

  shared_byte_buffer() = default;

  explicit shared_byte_buffer(std::shared_ptr<byte_buffer_interface const> bb)
      : bb_{std::move(bb)} {}

  uint8_t const* data() const { return bb_->data(); }

  size_t size() const { return bb_->size(); }

  size_t capacity() const { return bb_->capacity(); }

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

  mutable_byte_buffer() = default;

  explicit mutable_byte_buffer(
      std::shared_ptr<mutable_byte_buffer_interface> bb)
      : bb_{std::move(bb)} {}

  explicit operator bool() const noexcept { return static_cast<bool>(bb_); }

  uint8_t const* data() const { return bb_->data(); }

  uint8_t* data() { return bb_->mutable_data(); }

  size_t size() const { return bb_->size(); }

  size_t capacity() const { return bb_->capacity(); }

  bool empty() const { return bb_->size() == 0; }

  std::span<uint8_t const> span() const { return bb_->span(); }

  std::span<uint8_t> span() { return bb_->mutable_span(); }

  void clear() { bb_->clear(); }

  void reserve(size_t size) { bb_->reserve(size); }

  void resize(size_t size) { bb_->resize(size); }

  void shrink_to_fit() { bb_->shrink_to_fit(); }

  void freeze_location() { bb_->freeze_location(); }

  void append(void const* data, size_t size) { bb_->append(data, size); }

  template <detail::byte_range T>
  void append(T const& data) {
    append(data.data(), data.size());
  }

  internal::malloc_buffer& raw_buffer() { return bb_->raw_buffer(); }

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
