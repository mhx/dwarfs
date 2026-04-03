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
#include <cstdint>
#include <memory>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace dwarfs {

template <typename T, std::size_t MaxChunkBytes,
          bool PowerOfTwoElementsPerChunk = false>
class basic_chunked_append_only_vector {
  static_assert(MaxChunkBytes > 0);
  static_assert(!std::is_reference_v<T>);
  static_assert(!std::is_const_v<T>);
  static_assert(!std::is_volatile_v<T>);

 public:
  using value_type = T;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using reference = T&;
  using const_reference = T const&;

  static constexpr std::size_t
      chunk_elements_raw = MaxChunkBytes / sizeof(T) > 0
                               ? MaxChunkBytes / sizeof(T)
                               : 1;
  static constexpr std::size_t chunk_elements =
      PowerOfTwoElementsPerChunk ? std::bit_floor(chunk_elements_raw)
                                 : chunk_elements_raw;
  static constexpr std::size_t chunk_bytes = chunk_elements * sizeof(T);

  basic_chunked_append_only_vector() = default;
  ~basic_chunked_append_only_vector() { clear(); }

  basic_chunked_append_only_vector(basic_chunked_append_only_vector const&) =
      delete;
  basic_chunked_append_only_vector&
  operator=(basic_chunked_append_only_vector const&) = delete;

  basic_chunked_append_only_vector(
      basic_chunked_append_only_vector&& other) noexcept
      : chunks_{std::move(other.chunks_)}
      , size_{other.size_} {
    other.size_ = 0;
  }

  basic_chunked_append_only_vector&
  operator=(basic_chunked_append_only_vector&& other) noexcept {
    if (this != &other) {
      swap(other);
      other.clear();
    }
    return *this;
  }

  [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
  [[nodiscard]] size_type size() const noexcept { return size_; }

  void swap(basic_chunked_append_only_vector& other) noexcept {
    std::swap(chunks_, other.chunks_);
    std::swap(size_, other.size_);
  }

  [[nodiscard]] reference operator[](size_type i) noexcept {
    assert(i < size_);
    auto [c, o] = locate(i);
    return *ptr_at(c, o);
  }

  [[nodiscard]] const_reference operator[](size_type i) const noexcept {
    assert(i < size_);
    auto [c, o] = locate(i);
    return *ptr_at(c, o);
  }

  [[nodiscard]] reference at(size_type i) {
    if (i >= size_) {
      throw std::out_of_range("basic_chunked_append_only_vector::at");
    }
    return (*this)[i];
  }

  [[nodiscard]] const_reference at(size_type i) const {
    if (i >= size_) {
      throw std::out_of_range("basic_chunked_append_only_vector::at");
    }
    return (*this)[i];
  }

  [[nodiscard]] reference front() noexcept {
    assert(size_ > 0);
    return (*this)[0];
  }

  [[nodiscard]] const_reference front() const noexcept {
    assert(size_ > 0);
    return (*this)[0];
  }

  [[nodiscard]] reference back() noexcept {
    assert(size_ > 0);
    return (*this)[size_ - 1];
  }

  [[nodiscard]] const_reference back() const noexcept {
    assert(size_ > 0);
    return (*this)[size_ - 1];
  }

  template <class... Args>
  reference emplace_back(Args&&... args) {
    ensure_capacity_for_one_more();
    auto [c, o] = locate(size_);
    T* p = ptr_at(c, o);
    std::construct_at(p, std::forward<Args>(args)...);
    ++size_;
    return *p;
  }

  void clear() noexcept {
    destroy_constructed_elements();
    chunks_.clear();
    size_ = 0;
  }

 private:
  struct chunk {
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays)
    alignas(T) std::byte storage[chunk_elements][sizeof(T)];
  };

  [[nodiscard]] static constexpr std::pair<size_type, size_type>
  locate(size_type i) noexcept {
    return {i / chunk_elements, i % chunk_elements};
  }

  [[nodiscard]] T* ptr_at(size_type chunk_index, size_type offset) noexcept {
    auto* raw = &chunks_[chunk_index]->storage[offset];
    return std::launder(reinterpret_cast<T*>(raw));
  }

  [[nodiscard]] T const*
  ptr_at(size_type chunk_index, size_type offset) const noexcept {
    auto const* raw = &chunks_[chunk_index]->storage[offset];
    return std::launder(reinterpret_cast<T const*>(raw));
  }

  void ensure_capacity_for_one_more() {
    if (size_ == chunks_.size() * chunk_elements) {
      chunks_.push_back(std::make_unique<chunk>());
    }
  }

  void destroy_constructed_elements() noexcept {
    size_type remaining = size_;

    for (size_type chunk_index = 0;
         chunk_index < chunks_.size() && remaining > 0; ++chunk_index) {
      size_type const n =
          remaining < chunk_elements ? remaining : chunk_elements;
      for (size_type offset = 0; offset < n; ++offset) {
        std::destroy_at(ptr_at(chunk_index, offset));
      }
      remaining -= n;
    }
  }

  std::vector<std::unique_ptr<chunk>> chunks_;
  size_type size_{0};
};

template <typename T>
using chunked_append_only_vector = basic_chunked_append_only_vector<T, 4096>;

} // namespace dwarfs
