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

#include <cstddef>
#include <iterator>
#include <utility>

#include <dwarfs/container/small_vector.h>

namespace dwarfs::container {

/**
 * A minimal FIFO over small_vector, for short-lived, usually tiny queues
 *
 * std::deque allocates a whole block up front (512 bytes under libstdc++,
 * 4096 under libc++), even for a single element. That's often wasteful.
 *
 * Storage is reclaimed to the inline buffer whenever the queue drains, which
 * for a produce-a-few / consume-to-empty pattern means no allocation at all
 * after the first growth beyond N.
 */
template <typename T, std::size_t N>
class small_fifo {
 public:
  using value_type = T;

  static constexpr size_t inline_capacity = N;

  small_fifo() = default;

  template <std::input_iterator InputIt>
  small_fifo(InputIt first, InputIt last)
      : v_{first, last} {}

  bool empty() const noexcept { return head_ == v_.size(); }

  std::size_t size() const noexcept { return v_.size() - head_; }

  std::size_t capacity() const noexcept { return v_.capacity(); }

  T& front() noexcept { return v_[head_]; }
  T const& front() const noexcept { return v_[head_]; }

  void pop_front() {
    ++head_;

    if (head_ == v_.size()) {
      // drained: return to the inline buffer and reuse it next time round
      v_.clear();
      head_ = 0;
    } else if (head_ >= N && 2 * head_ >= v_.size()) {
      // amortized O(1): compact once dead slots outnumber live ones
      v_.erase(v_.begin(), v_.begin() + head_);
      head_ = 0;
    }
  }

  void push_back(T value) { v_.push_back(std::move(value)); }

  template <typename... Args>
  T& emplace_back(Args&&... args) {
    return v_.emplace_back(std::forward<Args>(args)...);
  }

  // bulk append: the common case is an empty queue, where the incoming
  // buffer can simply be adopted.
  template <typename Range>
  void append(Range&& r) {
    if (empty()) {
      clear();
    }
    for (auto&& e : std::forward<Range>(r)) {
      v_.push_back(std::move(e));
    }
  }

  void clear() noexcept {
    v_.clear();
    head_ = 0;
  }

 private:
  container::small_vector<T, N> v_;
  std::size_t head_{0};
};

} // namespace dwarfs::container
