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

#include <deque>
#include <future>
#include <stdexcept>
#include <utility>

#include <dwarfs/internal/ordered_preparation_queue.h>
#include <dwarfs/internal/worker_group.h>

namespace dwarfs::internal {

class ordered_preparation_queue_base::impl {
 public:
  impl(worker_group& workers, size_t capacity)
      : workers_{workers}
      , capacity_{capacity} {
    if (capacity == 0) {
      throw std::invalid_argument("ordered preparation queue capacity is zero");
    }
  }

  ~impl() {
    for (auto& future : jobs_) {
      future.wait();
    }
  }

  bool empty() const noexcept { return jobs_.empty(); }
  bool full() const noexcept { return jobs_.size() == capacity_; }
  size_t size() const noexcept { return jobs_.size(); }
  size_t capacity() const noexcept { return capacity_; }

  void submit(move_only_function<void()> job) {
    if (full()) {
      throw std::logic_error("ordered preparation queue is full");
    }

    std::packaged_task<void()> task(std::move(job));
    jobs_.push_back(task.get_future());
    try {
      if (!workers_.add_job(std::move(task))) {
        throw std::runtime_error("file preparation worker group stopped");
      }
    } catch (...) {
      jobs_.pop_back();
      throw;
    }
  }

  void pop() {
    if (empty()) {
      throw std::logic_error("ordered preparation queue is empty");
    }
    auto future = std::move(jobs_.front());
    jobs_.pop_front();
    future.get();
  }

 private:
  worker_group& workers_;
  size_t const capacity_;
  std::deque<std::future<void>> jobs_;
};

ordered_preparation_queue_base::ordered_preparation_queue_base(
    worker_group& workers, size_t capacity)
    : impl_{std::make_unique<impl>(workers, capacity)} {}

ordered_preparation_queue_base::~ordered_preparation_queue_base() = default;

bool ordered_preparation_queue_base::empty() const noexcept {
  return impl_->empty();
}

bool ordered_preparation_queue_base::full() const noexcept {
  return impl_->full();
}

size_t ordered_preparation_queue_base::size() const noexcept {
  return impl_->size();
}

size_t ordered_preparation_queue_base::capacity() const noexcept {
  return impl_->capacity();
}

void ordered_preparation_queue_base::submit_job(
    move_only_function<void()> job) {
  impl_->submit(std::move(job));
}

void ordered_preparation_queue_base::pop_job() { impl_->pop(); }

} // namespace dwarfs::internal
