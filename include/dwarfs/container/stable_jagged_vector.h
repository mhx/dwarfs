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

  static constexpr size_type no_block = std::numeric_limits<size_type>::max();

  struct block {
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays)
    std::unique_ptr<T[]> data;
    size_type size{0};
    size_type capacity{0};

    [[nodiscard]] size_type available() const noexcept {
      return capacity - size;
    }
  };

 public:
  static constexpr size_type block_elements =
      std::max<size_type>(BlockBytes / sizeof(T), 1);
  static constexpr size_type block_bytes = block_elements * sizeof(T);
  static constexpr size_type max_element_size =
      std::numeric_limits<size_type>::max() / sizeof(T);

  class reference {
   public:
    reference(reference const&) noexcept = default;
    reference(reference&&) noexcept = default;

    operator value_type() const { return load(); }

    [[nodiscard]] value_type load() const { return owner_->get(index_); }
    [[nodiscard]] auto data() const { return load().data(); }
    [[nodiscard]] auto size() const { return load().size(); }
    [[nodiscard]] bool empty() const { return load().empty(); }
    [[nodiscard]] auto begin() const { return load().begin(); }
    [[nodiscard]] auto end() const { return load().end(); }

    [[nodiscard]] decltype(auto) operator[](size_type i) const {
      return load()[i];
    }

    reference& operator=(reference const& other) = delete;
    reference& operator=(value_type) = delete;

    friend void swap(reference a, reference b) {
      if (a.owner_ != b.owner_) {
        throw std::invalid_argument(
            "cross-container swap is not supported for stable_jagged_vector");
      }

      a.owner_->swap_elements(a.index_, b.index_);
    }

   private:
    friend class stable_jagged_vector;

    reference(stable_jagged_vector& owner, size_type index) noexcept
        : owner_{&owner}
        , index_{index} {}

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

  [[nodiscard]] iterator begin() { return iterator::from_index(*this, 0); }

  [[nodiscard]] iterator end() { return iterator::from_index(*this, size()); }

  [[nodiscard]] const_iterator begin() const {
    return const_iterator::from_index(*this, 0);
  }

  [[nodiscard]] const_iterator end() const {
    return const_iterator::from_index(*this, size());
  }

  [[nodiscard]] const_iterator cbegin() const {
    return const_iterator::from_index(*this, 0);
  }

  [[nodiscard]] const_iterator cend() const {
    return const_iterator::from_index(*this, size());
  }

  [[nodiscard]] auto rbegin() { return std::reverse_iterator<iterator>{end()}; }

  [[nodiscard]] auto rend() { return std::reverse_iterator<iterator>{begin()}; }

  [[nodiscard]] auto rbegin() const {
    return std::reverse_iterator<const_iterator>{end()};
  }

  [[nodiscard]] auto rend() const {
    return std::reverse_iterator<const_iterator>{begin()};
  }

  [[nodiscard]] auto crbegin() const {
    return std::reverse_iterator<const_iterator>{cend()};
  }

  [[nodiscard]] auto crend() const {
    return std::reverse_iterator<const_iterator>{cbegin()};
  }

  [[nodiscard]] bool empty() const noexcept { return descriptors_.empty(); }
  [[nodiscard]] size_type size() const noexcept { return descriptors_.size(); }
  [[nodiscard]] size_type block_count() const noexcept {
    return blocks_.size();
  }

  [[nodiscard]] size_type size_in_bytes() const {
    return descriptors_.size_in_bytes() + payload_size_in_bytes() +
           blocks_.size() * sizeof(block);
  }

  [[nodiscard]] size_type capacity_in_bytes() const {
    return descriptors_.capacity_in_bytes() + payload_capacity_in_bytes() +
           blocks_.capacity() * sizeof(block);
  }

  [[nodiscard]] size_type payload_size_in_bytes() const noexcept {
    return live_payload_size_ * sizeof(T);
  }

  [[nodiscard]] size_type payload_capacity_in_bytes() const noexcept {
    size_type result = 0;
    for (auto const& b : blocks_) {
      result += b.capacity * sizeof(T);
    }
    return result;
  }

  [[nodiscard]] const_reference operator[](size_type i) const {
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

  [[nodiscard]] const_reference front() const {
    assert(!empty());
    return get(0);
  }

  [[nodiscard]] reference front() noexcept {
    assert(!empty());
    return reference{*this, 0};
  }

  [[nodiscard]] const_reference back() const {
    assert(!empty());
    return get(size() - 1);
  }

  [[nodiscard]] reference back() noexcept {
    assert(!empty());
    return reference{*this, size() - 1};
  }

  [[nodiscard]] const_reference get(size_type i) const {
    assert(i < size());
    auto const [block_index, offset, length] = descriptors_.get(i);

    if (length == 0) {
      return value_type{};
    }

    assert(block_index < blocks_.size());
    auto const& b = blocks_[block_index];
    assert(offset <= b.size);
    assert(length <= b.size - offset);

    return value_type{b.data.get() + offset, length};
  }

  template <typename R>
    requires detail::compatible_contiguous_range<R, T>
  reference emplace_back(R&& range) {
    auto&& r = std::forward<R>(range);
    auto const length = static_cast<size_type>(std::ranges::size(r));
    check_length(length);
    return append(std::span<T const>{std::ranges::data(r), length});
  }

  reference emplace_back(T const* data, size_type length) {
    if (length > 0 && data == nullptr) {
      throw std::invalid_argument(
          "stable_jagged_vector::emplace_back null data");
    }
    check_length(length);
    return append(std::span<T const>{data, length});
  }

  template <size_type N>
    requires detail::is_character_type_v<T>
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays)
  reference emplace_back(T const (&literal)[N]) {
    static_assert(N > 0);
    return emplace_back(literal, literal[N - 1] == T{} ? N - 1 : N);
  }

  void push_back(value_type value) {
    check_length(value.size());
    append(as_span(value));
  }

  void resize(size_type new_size) {
    for (auto i = new_size; i < size(); ++i) {
      live_payload_size_ -= element_length(i);
    }
    descriptors_.resize(new_size);
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
    live_payload_size_ = 0;
    fill_block_ = no_block;
  }

  void swap(stable_jagged_vector& other) noexcept {
    using std::swap;
    swap(descriptors_, other.descriptors_);
    swap(blocks_, other.blocks_);
    swap(live_payload_size_, other.live_payload_size_);
    swap(fill_block_, other.fill_block_);
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
  static void check_length(size_type length) {
    if (length > max_element_size) [[unlikely]] {
      throw std::length_error("stable_jagged_vector element too large");
    }
  }

  [[nodiscard]] static std::span<T const> as_span(value_type value) noexcept {
    return {value.data(), value.size()};
  }

  [[nodiscard]] static block make_block(size_type capacity) {
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays)
    return block{std::make_unique_for_overwrite<T[]>(capacity), 0, capacity};
  }

  [[nodiscard]] size_type element_length(size_type i) const {
    return descriptors_.template get_field<2>(i);
  }

  void set_descriptor(size_type i, descriptor_type d) {
    live_payload_size_ += std::get<2>(d);
    live_payload_size_ -= element_length(i);
    descriptors_.set(i, d);
  }

  reference append(std::span<T const> value) {
    descriptors_.push_back(append_payload(value));
    live_payload_size_ += value.size();
    return reference{*this, size() - 1};
  }

  [[nodiscard]] descriptor_type append_payload(std::span<T const> value) {
    assert(value.size() <= max_element_size);

    if (value.empty()) {
      return descriptor_type{};
    }

    size_type block_index;

    if (value.size() >= block_elements) {
      // add oversized values to their own block
      blocks_.push_back(make_block(value.size()));
      block_index = blocks_.size() - 1;
    } else {
      if (fill_block_ == no_block ||
          value.size() > blocks_[fill_block_].available()) {
        blocks_.push_back(make_block(block_elements));
        fill_block_ = blocks_.size() - 1;
      }
      block_index = fill_block_;
    }

    auto& b = blocks_[block_index];
    auto const offset = b.size;

    // T is trivially copyable
    std::memcpy(b.data.get() + offset, value.data(), value.size_bytes());

    b.size += value.size();

    return {block_index, offset, value.size()};
  }

  descriptor_vector_type descriptors_;
  std::vector<block> blocks_;
  size_type live_payload_size_{0};
  size_type fill_block_{no_block};
};

} // namespace dwarfs::container
