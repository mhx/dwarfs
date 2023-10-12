/* vim:set ts=2 sw=2 sts=2 et: */
/**
 * \author     Marcus Holland-Moritz (github@mhxnet.de)
 * \copyright  Copyright (c) Marcus Holland-Moritz
 *
 * This file is part of dwarfs.
 *
 * dwarfs is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * dwarfs is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with dwarfs.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <cstddef>
#include <future>
#include <limits>
#include <memory>
#include <utility>

#include <folly/Function.h>

namespace dwarfs {

/**
 * A group of worker threads
 *
 * This is an easy to use, multithreaded work dispatcher.
 * You can add jobs at any time and they will be dispatched
 * to the next available worker thread.
 */
class worker_group {
 public:
  using job_t = folly::Function<void()>;

  /**
   * Create a worker group
   *
   * \param num_workers     Number of worker threads.
   */
  explicit worker_group(
      const char* group_name, size_t num_workers = 1,
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
  size_t size() const { return impl_->size(); }
  size_t queue_size() const { return impl_->queue_size(); }
  double get_cpu_time() const { return impl_->get_cpu_time(); }

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
    virtual size_t size() const = 0;
    virtual size_t queue_size() const = 0;
    virtual double get_cpu_time() const = 0;
  };

 private:
  std::unique_ptr<impl> impl_;
};

} // namespace dwarfs
