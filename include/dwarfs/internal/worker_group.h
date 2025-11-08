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

#include <any>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <functional>
#include <future>
#include <limits>
#include <memory>
#include <optional>
#include <utility>
#include <variant>

#include <folly/Function.h>

namespace dwarfs {

class logger;
class os_access;

namespace internal {

class thread_state {
 public:
  virtual ~thread_state() = default;

  virtual void apply(std::any&& job_any) = 0;
};

template <typename... Args>
class basic_thread_state : public thread_state {
 public:
  using job_t = std::function<void(Args...)>;
  using moveonly_job_t = folly::Function<void(Args...)>;
  using any_job_t = std::variant<job_t, moveonly_job_t>;

  explicit basic_thread_state(Args... args)
      : args_(std::make_tuple(std::forward<Args>(args)...)) {}

  void apply(std::any&& job_any) override {
    std::visit(
        [this](auto&& j) {
          static_assert(std::is_rvalue_reference_v<decltype(j)>);
          auto job = std::forward<decltype(j)>(j);
          std::apply(job, args_);
        },
        std::move(
            *std::any_cast<std::shared_ptr<any_job_t>>(std::move(job_any))));
  }

  static std::any make_job(job_t&& job) {
    return std::any(std::make_shared<any_job_t>(std::move(job)));
  }

  static std::any make_job(moveonly_job_t&& job) {
    return std::any(std::make_shared<any_job_t>(std::move(job)));
  }

  template <std::invocable<Args...> T>
  static std::any make_job(T&& job) {
    return make_job(moveonly_job_t(std::forward<T>(job)));
  }

 private:
  std::tuple<Args...> args_;
};

/**
 * A group of worker threads
 *
 * This is an easy to use, multithreaded work dispatcher.
 * You can add jobs at any time and they will be dispatched
 * to the next available worker thread.
 */
class worker_group {
 public:
  /**
   * Create a worker group
   *
   * \param num_workers     Number of worker threads.
   */
  worker_group(logger& lgr, os_access const& os, char const* group_name,
               size_t num_workers = 1,
               size_t max_queue_len = std::numeric_limits<size_t>::max(),
               int niceness = 0);

  worker_group(
      logger& lgr, os_access const& os, char const* group_name,
      size_t num_workers,
      std::function<std::unique_ptr<thread_state>(size_t)> thread_state_factory,
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

  template <typename... Args>
  bool add_job(std::invocable<Args...> auto&& job) {
    return impl_->add_job(basic_thread_state<Args...>::make_job(
        std::forward<decltype(job)>(job)));
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
    virtual bool add_job(std::any&& job) = 0;
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
