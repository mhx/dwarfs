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
#include <deque>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include <dwarfs/internal/move_only_function.h>
#include <dwarfs/internal/worker_group_fwd.h>

namespace dwarfs::internal {

class ordered_preparation_queue_base {
 public:
  ordered_preparation_queue_base(worker_group& workers, size_t capacity);
  ~ordered_preparation_queue_base();

  ordered_preparation_queue_base(ordered_preparation_queue_base const&) =
      delete;
  ordered_preparation_queue_base&
  operator=(ordered_preparation_queue_base const&) = delete;

  bool empty() const noexcept;
  bool full() const noexcept;
  size_t size() const noexcept;
  size_t capacity() const noexcept;

 protected:
  void submit_job(move_only_function<void()> job);
  void pop_job();

 private:
  class impl;
  std::unique_ptr<impl> impl_;
};

template <typename T>
class ordered_preparation_queue final : public ordered_preparation_queue_base {
  static_assert(!std::is_void_v<T>);

 public:
  using task_type = move_only_function<T()>;

  ordered_preparation_queue(worker_group& workers, size_t capacity)
      : ordered_preparation_queue_base(workers, capacity) {}

  void submit(task_type task) {
    if (full()) {
      throw std::logic_error("ordered preparation queue is full");
    }

    auto result = std::make_shared<slot>();
    results_.push_back(result);

    try {
      submit_job([result, task = std::move(task)]() mutable {
        result->value.emplace(task());
      });
    } catch (...) {
      results_.pop_back();
      throw;
    }
  }

  T pop() {
    if (empty()) {
      throw std::logic_error("ordered preparation queue is empty");
    }

    auto result = std::move(results_.front());
    results_.pop_front();
    pop_job();

    return std::move(*result->value);
  }

 private:
  struct slot {
    std::optional<T> value;
  };

  std::deque<std::shared_ptr<slot>> results_;
};

template <typename Range, typename Prepare>
class prepared_range {
  using source_iterator = decltype(std::begin(std::declval<Range&>()));
  using input_type =
      std::remove_cvref_t<decltype(*std::declval<source_iterator&>())>;
  using result_type = std::invoke_result_t<Prepare&, input_type>;
  using pair_type = std::pair<input_type, result_type>;

 public:
  prepared_range(Range& source, worker_group* workers, size_t capacity,
                 Prepare prepare)
      : next_{std::begin(source)}
      , end_{std::end(source)}
      , prepare_{std::move(prepare)} {
    if (workers) {
      pending_.emplace(*workers, capacity);
    }
  }

  prepared_range(prepared_range const&) = delete;
  prepared_range& operator=(prepared_range const&) = delete;

  class iterator {
   public:
    using iterator_concept = std::input_iterator_tag;
    using iterator_category = std::input_iterator_tag;
    using difference_type = std::ptrdiff_t;
    using value_type = pair_type;

    iterator() = default;
    explicit iterator(prepared_range* owner)
        : owner_{owner} {}

    pair_type& operator*() const { return *owner_->current_; }

    iterator& operator++() {
      owner_->advance();
      return *this;
    }

    void operator++(int) { ++*this; }

    friend bool operator==(iterator const& it, std::default_sentinel_t) {
      return it.at_end();
    }

   private:
    bool at_end() const noexcept { return !owner_ || !owner_->current_; }

    prepared_range* owner_{nullptr};
  };

  iterator begin() {
    if (!started_) {
      started_ = true;
      advance();
    }

    return iterator{this};
  }

  std::default_sentinel_t end() const noexcept { return {}; }

 private:
  void fill() {
    while (next_ != end_ && !pending_->full()) {
      auto input = *next_++;
      pending_->submit(
          [input = std::move(input), prepare = prepare_]() mutable {
            auto output = std::invoke(prepare, input);
            return pair_type{std::move(input), std::move(output)};
          });
    }
  }

  void advance() {
    current_.reset();

    if (pending_) {
      fill();

      if (!pending_->empty()) {
        current_.emplace(pending_->pop());
        fill();
      }
    } else if (next_ != end_) {
      auto input = *next_++;
      auto output = std::invoke(prepare_, input);
      current_.emplace(std::move(input), std::move(output));
    }
  }

  source_iterator next_;
  source_iterator end_;
  Prepare prepare_;
  std::optional<pair_type> current_;
  std::optional<ordered_preparation_queue<pair_type>> pending_;
  bool started_{false};
};

template <typename Range, typename Prepare>
auto prepare_ordered(Range& source, worker_group* workers, size_t capacity,
                     Prepare&& prepare) {
  return prepared_range<Range, std::decay_t<Prepare>>(
      source, workers, capacity, std::forward<Prepare>(prepare));
}

} // namespace dwarfs::internal
