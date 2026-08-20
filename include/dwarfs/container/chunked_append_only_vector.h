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
#include <optional>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include <dwarfs/container/detail/index_based_iterator.h>

namespace dwarfs::container {

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
  using iterator =
      detail::index_based_iterator<basic_chunked_append_only_vector>;
  using const_iterator =
      detail::index_based_const_iterator<basic_chunked_append_only_vector>;

  static constexpr std::size_t
      chunk_elements_raw = MaxChunkBytes / sizeof(T) > 0
                               ? MaxChunkBytes / sizeof(T)
                               : 1;
  static constexpr std::size_t chunk_elements =
      PowerOfTwoElementsPerChunk ? std::bit_floor(chunk_elements_raw)
                                 : chunk_elements_raw;
  static constexpr std::size_t chunk_bytes = chunk_elements * sizeof(T);

 private:
  struct chunk_deleter {
    void operator()(T* p) const noexcept {
      std::allocator<T>{}.deallocate(p, chunk_elements);
    }
  };

  using chunk_ptr = std::unique_ptr<T, chunk_deleter>;

  [[nodiscard]] static chunk_ptr allocate_chunk() {
    return chunk_ptr{std::allocator<T>{}.allocate(chunk_elements)};
  }

 public:
  class drain_type {
   public:
    drain_type(drain_type const&) = delete;
    drain_type& operator=(drain_type const&) = delete;

    drain_type(drain_type&& other) noexcept
        : chunks_{std::move(other.chunks_)}
        , size_{std::exchange(other.size_, 0)}
        , next_chunk_index_{std::exchange(other.next_chunk_index_, 0)}
        , current_chunk_index_{std::exchange(other.current_chunk_index_, 0)}
        , current_chunk_size_{std::exchange(other.current_chunk_size_, 0)} {}

    drain_type& operator=(drain_type&& other) noexcept {
      if (this != &other) {
        clear();
        chunks_ = std::move(other.chunks_);
        size_ = std::exchange(other.size_, 0);
        next_chunk_index_ = std::exchange(other.next_chunk_index_, 0);
        current_chunk_index_ = std::exchange(other.current_chunk_index_, 0);
        current_chunk_size_ = std::exchange(other.current_chunk_size_, 0);
      }
      return *this;
    }

    ~drain_type() { clear(); }

    // The returned span remains valid until the next call to next_chunk(),
    // or until this drain object is moved from or destroyed.
    [[nodiscard]] std::optional<std::span<T>> next_chunk() noexcept {
      release_current_chunk();

      if (next_chunk_index_ >= chunks_.size()) {
        return std::nullopt;
      }

      size_type const remaining = size_ - next_chunk_index_ * chunk_elements;
      size_type const n =
          remaining < chunk_elements ? remaining : chunk_elements;

      current_chunk_index_ = next_chunk_index_++;
      current_chunk_size_ = n;

      return std::span<T>{chunks_[current_chunk_index_].get(), n};
    }

   private:
    friend class basic_chunked_append_only_vector;

    drain_type(std::vector<chunk_ptr>&& chunks, size_type size) noexcept
        : chunks_{std::move(chunks)}
        , size_{size} {}

    void release_current_chunk() noexcept {
      if (current_chunk_size_ == 0) {
        return;
      }

      auto& current = chunks_[current_chunk_index_];
      for (size_type offset = 0; offset < current_chunk_size_; ++offset) {
        std::destroy_at(current.get() + offset);
      }
      current.reset();
      current_chunk_size_ = 0;
    }

    void clear() noexcept {
      release_current_chunk();

      for (size_type chunk_index = next_chunk_index_;
           chunk_index < chunks_.size(); ++chunk_index) {
        size_type const remaining = size_ - chunk_index * chunk_elements;
        size_type const n =
            remaining < chunk_elements ? remaining : chunk_elements;
        auto& current = chunks_[chunk_index];
        for (size_type offset = 0; offset < n; ++offset) {
          std::destroy_at(current.get() + offset);
        }
        current.reset();
      }

      chunks_.clear();
      size_ = 0;
      next_chunk_index_ = 0;
    }

    std::vector<chunk_ptr> chunks_;
    size_type size_{0};
    size_type next_chunk_index_{0};
    size_type current_chunk_index_{0};
    size_type current_chunk_size_{0};
  };

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

  [[nodiscard]] iterator begin() noexcept {
    return iterator::from_index(*this, 0);
  }

  [[nodiscard]] iterator end() noexcept {
    return iterator::from_index(*this, size());
  }

  [[nodiscard]] const_iterator begin() const noexcept {
    return const_iterator::from_index(*this, 0);
  }

  [[nodiscard]] const_iterator end() const noexcept {
    return const_iterator::from_index(*this, size());
  }

  [[nodiscard]] const_iterator cbegin() const noexcept {
    return const_iterator::from_index(*this, 0);
  }

  [[nodiscard]] const_iterator cend() const noexcept {
    return const_iterator::from_index(*this, size());
  }

  [[nodiscard]] auto rbegin() noexcept {
    return std::reverse_iterator<iterator>{end()};
  }

  [[nodiscard]] auto rend() noexcept {
    return std::reverse_iterator<iterator>{begin()};
  }

  [[nodiscard]] auto rbegin() const noexcept {
    return std::reverse_iterator<const_iterator>{end()};
  }

  [[nodiscard]] auto rend() const noexcept {
    return std::reverse_iterator<const_iterator>{begin()};
  }

  [[nodiscard]] auto crbegin() const noexcept {
    return std::reverse_iterator<const_iterator>{cend()};
  }

  [[nodiscard]] auto crend() const noexcept {
    return std::reverse_iterator<const_iterator>{cbegin()};
  }

  [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
  [[nodiscard]] size_type size() const noexcept { return size_; }
  [[nodiscard]] size_type capacity() const noexcept {
    return chunks_.size() * chunk_elements;
  }

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

  template <typename... Args>
  reference emplace_back(Args&&... args) {
    ensure_capacity_for_one_more();
    auto [c, o] = locate(size_);
    T* p = ptr_at(c, o);
    std::construct_at(p, std::forward<Args>(args)...);
    ++size_;
    return *p;
  }

  // Transfer ownership of all chunks to a destructive chunk iterator.
  [[nodiscard]] drain_type drain() && noexcept {
    return drain_type{std::move(chunks_), std::exchange(size_, 0)};
  }

  [[nodiscard]] drain_type drain() & = delete;

  void clear() noexcept {
    destroy_constructed_elements();
    chunks_.clear();
    size_ = 0;
  }

  void resize(size_type new_size)
    requires std::default_initializable<T>
  {
    if (new_size < size_) {
      while (size_ > new_size) {
        --size_;
        auto [c, o] = locate(size_);
        std::destroy_at(ptr_at(c, o));
      }

      size_type const needed_chunks =
          new_size == 0 ? 0 : locate(new_size - 1).first + 1;
      chunks_.resize(needed_chunks);
    } else if (new_size > size_) {
      while (new_size > chunks_.size() * chunk_elements) {
        chunks_.push_back(allocate_chunk());
      }

      size_type const old_size = size_;

      try {
        while (size_ < new_size) {
          auto [c, o] = locate(size_);
          std::construct_at(ptr_at(c, o));
          ++size_;
        }
      } catch (...) {
        while (size_ > old_size) {
          --size_;
          auto [c, o] = locate(size_);
          std::destroy_at(ptr_at(c, o));
        }
        throw;
      }
    }
  }

 private:
  [[nodiscard]] static constexpr std::pair<size_type, size_type>
  locate(size_type i) noexcept {
    return {i / chunk_elements, i % chunk_elements};
  }

  [[nodiscard]] T* ptr_at(size_type chunk_index, size_type offset) noexcept {
    return chunks_[chunk_index].get() + offset;
  }

  [[nodiscard]] T const*
  ptr_at(size_type chunk_index, size_type offset) const noexcept {
    return chunks_[chunk_index].get() + offset;
  }

  void ensure_capacity_for_one_more() {
    if (size_ == chunks_.size() * chunk_elements) {
      chunks_.push_back(allocate_chunk());
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

  std::vector<chunk_ptr> chunks_;
  size_type size_{0};
};

template <typename T>
using chunked_append_only_vector = basic_chunked_append_only_vector<T, 65536>;

} // namespace dwarfs::container
