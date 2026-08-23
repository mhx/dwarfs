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

#include <algorithm>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstring>
#include <iterator>
#include <limits>
#include <memory>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <dwarfs/container/detail/index_based_iterator.h>
#include <dwarfs/container/segmented_packed_int_vector.h>

namespace dwarfs::container {

namespace detail {

template <typename T>
constexpr inline bool is_character_type_v =
    std::same_as<T, char> || std::same_as<T, wchar_t> ||
    std::same_as<T, char8_t> || std::same_as<T, char16_t> ||
    std::same_as<T, char32_t>;

template <typename T>
using immutable_sequence_view =
    std::conditional_t<is_character_type_v<T>, std::basic_string_view<T>,
                       std::span<T const>>;

template <typename R, typename T>
concept compatible_contiguous_range =
    std::ranges::contiguous_range<R> && std::ranges::sized_range<R> &&
    std::same_as<std::remove_cv_t<std::ranges::range_value_t<R>>, T>;

} // namespace detail

/**
 * Vector-like container for immutable variable-length sequences with stable
 * backing storage.
 *
 * Each logical element is represented by a compact (block, offset, size)
 * descriptor. Descriptors are stored in a segmented packed integer vector,
 * while payload data is appended to stable heap blocks.
 */
template <typename T, std::size_t BlockBytes = 65536>
  requires(std::is_trivially_copyable_v<T> && !std::is_const_v<T> &&
           !std::is_volatile_v<T> && !std::same_as<T, bool>)
class stable_jagged_vector {
  static_assert(BlockBytes > 0);

 public:
  using element_type = T;
  using value_type = detail::immutable_sequence_view<T>;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using const_reference = value_type;

 private:
  using descriptor_type = std::tuple<size_type, size_type, size_type>;
  using descriptor_vector_type = segmented_packed_int_vector<descriptor_type>;

  struct block_deleter {
    size_type capacity{0};

    void operator()(T* p) const noexcept {
      if (p != nullptr) {
        std::allocator<T>{}.deallocate(p, capacity);
      }
    }
  };

  using block_ptr = std::unique_ptr<T, block_deleter>;

  struct block {
    block_ptr data;
    size_type size{0};

    [[nodiscard]] size_type capacity() const noexcept {
      return data.get_deleter().capacity;
    }
  };

 public:
  static constexpr size_type block_elements_raw = BlockBytes / sizeof(T) > 0
                                                      ? BlockBytes / sizeof(T)
                                                      : 1;
  static constexpr size_type block_elements = block_elements_raw;
  static constexpr size_type block_bytes = block_elements * sizeof(T);

  class reference {
   public:
    reference(reference const&) noexcept = default;
    reference(reference&&) noexcept = default;

    operator value_type() const noexcept { return load(); }

    [[nodiscard]] value_type load() const noexcept {
      return owner_->get(index_);
    }
    [[nodiscard]] auto data() const noexcept { return load().data(); }
    [[nodiscard]] auto size() const noexcept { return load().size(); }
    [[nodiscard]] bool empty() const noexcept { return load().empty(); }
    [[nodiscard]] auto begin() const noexcept { return load().begin(); }
    [[nodiscard]] auto end() const noexcept { return load().end(); }

    [[nodiscard]] decltype(auto) operator[](size_type i) const noexcept {
      return load()[i];
    }

    reference& operator=(reference const& other) {
      assign_from_proxy(other);
      return *this;
    }

    reference const& operator=(reference const& other) const {
      assign_from_proxy(other);
      return *this;
    }

    reference& operator=(value_type value) {
      owner_->set(index_, value);
      return *this;
    }

    reference const& operator=(value_type value) const {
      owner_->set(index_, value);
      return *this;
    }

    friend void swap(reference a, reference b) {
      if (a.owner_ == b.owner_) {
        a.owner_->swap_elements(a.index_, b.index_);
        return;
      }

      // Cross-container swap cannot exchange descriptors because descriptors
      // refer to different block arrays. Copy each immutable payload instead.
      auto const av = a.load();
      auto const bv = b.load();
      a.owner_->set(a.index_, bv);
      b.owner_->set(b.index_, av);
    }

    friend bool operator==(reference const& lhs, value_type rhs)
      requires requires(value_type a, value_type b) { a == b; }
    {
      return lhs.load() == rhs;
    }

    friend bool operator==(value_type lhs, reference const& rhs)
      requires requires(value_type a, value_type b) { a == b; }
    {
      return lhs == rhs.load();
    }

   private:
    friend class stable_jagged_vector;

    reference(stable_jagged_vector& owner, size_type index) noexcept
        : owner_{&owner}
        , index_{index} {}

    void assign_from_proxy(reference const& other) const {
      if (owner_ == other.owner_) {
        owner_->descriptors_.set(index_,
                                 other.owner_->descriptors_.get(other.index_));
      } else {
        owner_->set(index_, other.load());
      }
    }

    stable_jagged_vector* owner_;
    size_type index_;
  };

  using iterator = detail::index_based_iterator<stable_jagged_vector>;
  using const_iterator =
      detail::index_based_const_iterator<stable_jagged_vector>;

  stable_jagged_vector() = default;
  ~stable_jagged_vector() = default;

  stable_jagged_vector(stable_jagged_vector const&) = delete;
  stable_jagged_vector& operator=(stable_jagged_vector const&) = delete;

  stable_jagged_vector(stable_jagged_vector&& other) noexcept { swap(other); }

  stable_jagged_vector& operator=(stable_jagged_vector&& other) noexcept {
    if (this != &other) {
      clear();
      swap(other);
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

  [[nodiscard]] bool empty() const noexcept { return descriptors_.empty(); }
  [[nodiscard]] size_type size() const noexcept { return descriptors_.size(); }
  [[nodiscard]] size_type block_count() const noexcept {
    return blocks_.size();
  }

  [[nodiscard]] size_type size_in_bytes() const noexcept {
    return descriptors_.size_in_bytes() + payload_size_in_bytes() +
           blocks_.size() * sizeof(block);
  }

  [[nodiscard]] size_type capacity_in_bytes() const noexcept {
    return descriptors_.capacity_in_bytes() + payload_capacity_in_bytes() +
           blocks_.capacity() * sizeof(block);
  }

  [[nodiscard]] size_type payload_size_in_bytes() const noexcept {
    return total_payload_size_ * sizeof(T);
  }

  [[nodiscard]] size_type payload_capacity_in_bytes() const noexcept {
    size_type result = 0;
    for (auto const& b : blocks_) {
      result += b.capacity() * sizeof(T);
    }
    return result;
  }

  [[nodiscard]] const_reference operator[](size_type i) const noexcept {
    assert(i < size());
    return get(i);
  }

  [[nodiscard]] reference operator[](size_type i) noexcept {
    assert(i < size());
    return reference{*this, i};
  }

  [[nodiscard]] const_reference at(size_type i) const {
    if (i >= size()) {
      throw std::out_of_range("stable_jagged_vector::at");
    }
    return get(i);
  }

  [[nodiscard]] reference at(size_type i) {
    if (i >= size()) {
      throw std::out_of_range("stable_jagged_vector::at");
    }
    return reference{*this, i};
  }

  [[nodiscard]] const_reference front() const noexcept {
    assert(!empty());
    return get(0);
  }

  [[nodiscard]] reference front() noexcept {
    assert(!empty());
    return reference{*this, 0};
  }

  [[nodiscard]] const_reference back() const noexcept {
    assert(!empty());
    return get(size() - 1);
  }

  [[nodiscard]] reference back() noexcept {
    assert(!empty());
    return reference{*this, size() - 1};
  }

  [[nodiscard]] const_reference get(size_type i) const noexcept {
    assert(i < size());
    auto const [block_index, offset, length] = descriptors_.get(i);

    if (length == 0) {
      return value_type{};
    }

    assert(block_index < blocks_.size());
    auto const& b = blocks_[block_index];
    assert(offset <= b.size);
    assert(length <= b.size - offset);

    return make_view(b.data.get() + offset, length);
  }

  void set(size_type i, value_type value) {
    assert(i < size());
    descriptors_.set(i, append_payload(as_span(value)));
  }

  template <typename R>
    requires detail::compatible_contiguous_range<R, T>
  reference emplace_back(R&& range) {
    auto const view =
        std::span<T const>{std::ranges::data(range), std::ranges::size(range)};
    descriptors_.push_back(append_payload(view));
    return reference{*this, size() - 1};
  }

  reference emplace_back(T const* data, size_type length) {
    if (length > 0 && data == nullptr) {
      throw std::invalid_argument(
          "stable_jagged_vector::emplace_back null data");
    }
    descriptors_.push_back(append_payload(std::span<T const>{data, length}));
    return reference{*this, size() - 1};
  }

  template <size_type N>
    requires detail::is_character_type_v<T>
  reference emplace_back(T const (&literal)[N]) {
    static_assert(N > 0);
    size_type const length = literal[N - 1] == T{} ? N - 1 : N;
    return emplace_back(literal, length);
  }

  void push_back(value_type value) {
    descriptors_.push_back(append_payload(as_span(value)));
  }

  void resize(size_type new_size) {
    if (new_size <= size()) {
      descriptors_.resize(new_size);
    } else {
      descriptors_.resize(new_size, descriptor_type{});
    }
  }

  void reserve(size_type n) { descriptors_.reserve(n); }

  void shrink_to_fit() {
    descriptors_.shrink_to_fit();
    blocks_.shrink_to_fit();
  }

  void optimize_storage() {
    descriptors_.optimize_storage();
    blocks_.shrink_to_fit();
  }

  void clear() noexcept {
    descriptors_.clear();
    blocks_.clear();
    total_payload_size_ = 0;
  }

  void swap(stable_jagged_vector& other) noexcept {
    using std::swap;
    swap(descriptors_, other.descriptors_);
    swap(blocks_, other.blocks_);
    swap(total_payload_size_, other.total_payload_size_);
  }

  friend void
  swap(stable_jagged_vector& lhs, stable_jagged_vector& rhs) noexcept {
    lhs.swap(rhs);
  }

  void swap_elements(size_type a, size_type b) {
    assert(a < size());
    assert(b < size());

    if (a != b) {
      auto const tmp = descriptors_.get(a);
      descriptors_.set(a, descriptors_.get(b));
      descriptors_.set(b, tmp);
    }
  }

 private:
  [[nodiscard]] static value_type
  make_view(T const* data, size_type size) noexcept {
    if constexpr (detail::is_character_type_v<T>) {
      return std::basic_string_view<T>{data, size};
    } else {
      return std::span<T const>{data, size};
    }
  }

  [[nodiscard]] static std::span<T const> as_span(value_type value) noexcept {
    return {value.data(), value.size()};
  }

  [[nodiscard]] static block make_block(size_type capacity) {
    auto* p = std::allocator<T>{}.allocate(capacity);
    return block{block_ptr{p, block_deleter{capacity}}, 0};
  }

  [[nodiscard]] descriptor_type append_payload(std::span<T const> value) {
    if (value.empty()) {
      return descriptor_type{};
    }

    if (value.size() > std::numeric_limits<size_type>::max() / sizeof(T) ||
        total_payload_size_ >
            std::numeric_limits<size_type>::max() - value.size()) [[unlikely]] {
      throw std::length_error("stable_jagged_vector element too large");
    }

    if (blocks_.empty() ||
        value.size() > blocks_.back().capacity() - blocks_.back().size) {
      blocks_.push_back(make_block(std::max(value.size(), block_elements)));
    }

    size_type const block_index = blocks_.size() - 1;

    auto& b = blocks_[block_index];
    auto const offset = b.size;

    // T is trivially copyable
    std::memcpy(b.data.get() + offset, value.data(), value.size_bytes());

    b.size += value.size();
    total_payload_size_ += value.size();

    return {block_index, offset, value.size()};
  }

  descriptor_vector_type descriptors_;
  std::vector<block> blocks_;
  size_type total_payload_size_{0};
};

} // namespace dwarfs::container
