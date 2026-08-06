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

#include <bit>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstring>
#include <functional>
#include <memory>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <dwarfs/container/detail/concepts.h>

namespace dwarfs::container {

namespace detail {

template <byte_range R>
[[nodiscard]] inline std::span<std::byte const>
to_byte_span(R const& r) noexcept {
  return {reinterpret_cast<std::byte const*>(std::ranges::data(r)),
          std::ranges::size(r)};
}

} // namespace detail

/**
 * Content-based hash for byte ranges.
 *
 * Transparent, so an index over std::span<std::byte const> can be probed
 * with any byte_range without materializing a value first.
 */
struct byte_span_hash {
  using is_transparent = void;

  template <detail::byte_range R>
  [[nodiscard]] std::size_t operator()(R const& r) const noexcept {
    auto const s = detail::to_byte_span(r);
    if (s.empty()) {
      return 0;
    }
    return std::hash<std::string_view>{}(
        std::string_view{reinterpret_cast<char const*>(s.data()), s.size()});
  }
};

/**
 * Content-based equality for byte ranges.
 */
struct byte_span_equal {
  using is_transparent = void;

  template <detail::byte_range A, detail::byte_range B>
  [[nodiscard]] bool operator()(A const& a, B const& b) const noexcept {
    auto const x = detail::to_byte_span(a);
    auto const y = detail::to_byte_span(b);
    return x.size() == y.size() &&
           (x.empty() || std::memcmp(x.data(), y.data(), x.size()) == 0);
  }
};

template <std::size_t ChunkSize>
class pinned_byte_span_store {
 public:
  static_assert(ChunkSize > 0);
  static_assert(std::has_single_bit(ChunkSize),
                "ChunkSize must be a power of 2");

  using value_type = std::byte;
  using size_type = std::size_t;
  using reference = std::span<std::byte>;
  using const_reference = std::span<std::byte const>;

  explicit pinned_byte_span_store(size_type span_size)
      : span_size_{span_size} {
    if (span_size_ == 0) {
      throw std::invalid_argument(
          "pinned_byte_span_store: span_size must not be zero");
    }
  }

  pinned_byte_span_store(pinned_byte_span_store const&) = delete;
  pinned_byte_span_store& operator=(pinned_byte_span_store const&) = delete;

  pinned_byte_span_store(pinned_byte_span_store&& other) noexcept
      : chunks_{std::move(other.chunks_)}
      , span_size_{other.span_size_}
      , size_{other.size_} {
    other.size_ = 0;
  }

  pinned_byte_span_store& operator=(pinned_byte_span_store&& other) noexcept {
    if (this != &other) {
      chunks_ = std::move(other.chunks_);
      span_size_ = other.span_size_;
      size_ = other.size_;
      other.size_ = 0;
    }
    return *this;
  }

  [[nodiscard]] size_type span_size() const noexcept { return span_size_; }

  [[nodiscard]] size_type size() const noexcept { return size_; }

  [[nodiscard]] size_type size_in_bytes() const noexcept {
    return size_ * span_size_;
  }

  [[nodiscard]] bool empty() const noexcept { return size_ == 0; }

  [[nodiscard]] size_type capacity() const noexcept {
    return chunks_.size() * ChunkSize;
  }

  void reserve(size_type n) {
    auto const needed = (n + ChunkSize - 1) / ChunkSize;
    chunks_.reserve(needed);
    while (chunks_.size() < needed) {
      // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays)
      chunks_.push_back(
          std::make_unique_for_overwrite<std::byte[]>(bytes_per_chunk()));
    }
  }

  [[nodiscard]] reference emplace_back() {
    ensure_capacity_for_one_more();
    std::byte* p = ptr_at(size_);
    ++size_;
    return {p, span_size_};
  }

  /**
   * Append a copy of `data`, which must be exactly span_size() bytes.
   *
   * Strong exception guarantee: on failure the store is unchanged.
   */
  template <detail::byte_range R>
  reference emplace_back(R const& data) {
    auto const src = detail::to_byte_span(data);

    if (src.size() != span_size_) {
      throw std::invalid_argument(
          "pinned_byte_span_store::emplace_back: size mismatch");
    }

    ensure_capacity_for_one_more();
    std::byte* p = ptr_at(size_);
    std::memcpy(p, src.data(), span_size_);
    ++size_;

    return {p, span_size_};
  }

  void pop_back() noexcept {
    assert(size_ > 0);
    --size_;
  }

  [[nodiscard]] reference operator[](size_type index) noexcept {
    assert(index < size_);
    return {ptr_at(index), span_size_};
  }

  [[nodiscard]] const_reference operator[](size_type index) const noexcept {
    assert(index < size_);
    return {ptr_at(index), span_size_};
  }

  [[nodiscard]] reference at(size_type index) {
    if (index >= size_) {
      throw std::out_of_range("pinned_byte_span_store::at");
    }
    return {ptr_at(index), span_size_};
  }

  [[nodiscard]] const_reference at(size_type index) const {
    if (index >= size_) {
      throw std::out_of_range("pinned_byte_span_store::at");
    }
    return {ptr_at(index), span_size_};
  }

 private:
  [[nodiscard]] static constexpr size_type
  chunk_index(size_type index) noexcept {
    return index / ChunkSize;
  }

  [[nodiscard]] static constexpr size_type
  chunk_offset(size_type index) noexcept {
    return index & (ChunkSize - 1);
  }

  [[nodiscard]] size_type bytes_per_chunk() const noexcept {
    return span_size_ * ChunkSize;
  }

  [[nodiscard]] std::byte* ptr_at(size_type index) noexcept {
    auto const c = chunk_index(index);
    auto const o = chunk_offset(index);
    return chunks_[c].get() + o * span_size_;
  }

  [[nodiscard]] std::byte const* ptr_at(size_type index) const noexcept {
    auto const c = chunk_index(index);
    auto const o = chunk_offset(index);
    return chunks_[c].get() + o * span_size_;
  }

  void ensure_capacity_for_one_more() {
    if (size_ == chunks_.size() * ChunkSize) {
      // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays)
      chunks_.push_back(
          std::make_unique_for_overwrite<std::byte[]>(bytes_per_chunk()));
    }
  }

  // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays)
  std::vector<std::unique_ptr<std::byte[]>> chunks_;
  size_type span_size_;
  size_type size_{0};
};

} // namespace dwarfs::container
