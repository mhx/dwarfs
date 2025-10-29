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

#include <chrono>
#include <concepts>
#include <cstddef>
#include <functional>
#include <future>
#include <limits>
#include <memory>
#include <optional>
#include <utility>

namespace dwarfs {

class logger;
class os_access;

namespace internal {

/**
 * A group of worker threads
 *
 * This is an easy to use, multithreaded work dispatcher.
 * You can add jobs at any time and they will be dispatched
 * to the next available worker thread.
 */
class worker_group {
 public:
  using job_t = std::function<void()>;
  using moveonly_job_t = std::move_only_function<void()>;

  /**
   * Create a worker group
   *
   * \param num_workers     Number of worker threads.
   */
  explicit worker_group(
      logger& lgr, os_access const& os, char const* group_name,
      size_t num_workers = 1,
      size_t max_queue_len = std::numeric_limits<size_t>::max(),
      int niceness = 0);

  worker_group() = default;
  ~worker_group() = default;

  worker_group(worker_group&&) = default;
  worker_group& operator=(worker_group&&) = default;

  explicit operator bool() const { return static_cast<bool>(impl_); }

  void stop() { impl_->stop(); }
  void wait() { impl_->wait(); }
  bool running() const { return impl_->running(); }

  bool add_job(job_t&& job) { return impl_->add_job(std::move(job)); }
  bool add_job(moveonly_job_t&& job) {
    return impl_->add_moveonly_job(std::move(job));
  }

  template <std::invocable T>
  bool add_job(T&& job) {
    return add_job(moveonly_job_t{std::forward<T>(job)});
  }

  size_t size() const { return impl_->size(); }
  size_t queue_size() const { return impl_->queue_size(); }

  std::chrono::nanoseconds get_cpu_time(std::error_code& ec) const {
    return impl_->get_cpu_time(ec);
  }

  std::optional<std::chrono::nanoseconds> try_get_cpu_time() const {
    return impl_->try_get_cpu_time();
  }

  bool set_affinity(std::vector<int> const& cpus) {
    return impl_->set_affinity(cpus);
  }

  template <typename T>
  bool add_job(std::packaged_task<T()>&& task) {
    return add_job([task = std::move(task)]() mutable { task(); });
  }

  class impl {
   public:
    virtual ~impl() = default;

    virtual void stop() = 0;
    virtual void wait() = 0;
    virtual bool running() const = 0;
    virtual bool add_job(job_t&& job) = 0;
    virtual bool add_moveonly_job(moveonly_job_t&& job) = 0;
    virtual size_t size() const = 0;
    virtual size_t queue_size() const = 0;
    virtual std::chrono::nanoseconds
    get_cpu_time(std::error_code& ec) const = 0;
    virtual std::optional<std::chrono::nanoseconds>
    try_get_cpu_time() const = 0;
    virtual bool set_affinity(std::vector<int> const& cpus) = 0;
  };

 private:
  std::unique_ptr<impl> impl_;
};

} // namespace internal
} // namespace dwarfs
