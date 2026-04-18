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
#include <cstddef>
#include <memory>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace dwarfs::container {

template <std::size_t ChunkSize>
class pinned_byte_span_store {
 public:
  static_assert(ChunkSize > 0);
  static_assert(std::has_single_bit(ChunkSize),
                "ChunkSize must be a power of 2");

  using value_type = std::byte;
  using size_type = std::size_t;

  explicit pinned_byte_span_store(size_type span_size)
      : span_size_{span_size} {
    assert(span_size_ > 0);
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

  [[nodiscard]] std::span<std::byte> emplace_back() {
    ensure_capacity_for_one_more();
    std::byte* p = ptr_at(size_);
    ++size_;
    return {p, span_size_};
  }

  [[nodiscard]] std::span<std::byte> at(size_type index) {
    if (index >= size_) {
      throw std::out_of_range("pinned_byte_span_store::at");
    }
    return {ptr_at(index), span_size_};
  }

  [[nodiscard]] std::span<std::byte const> at(size_type index) const {
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
      chunks_.push_back(std::make_unique<std::byte[]>(bytes_per_chunk()));
    }
  }

  // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays)
  std::vector<std::unique_ptr<std::byte[]>> chunks_;
  size_type span_size_;
  size_type size_{0};
};

} // namespace dwarfs::container
