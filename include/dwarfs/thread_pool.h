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
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <system_error>

namespace dwarfs {

class logger;
class os_access;

namespace internal {

class worker_group;

} // namespace internal

/**
 * A thread pool
 *
 * This class is mostly a wrapper around internal::worker_group as we
 * currently don't want to expose that API directly.
 */
class thread_pool {
 public:
  using job_type = std::function<void()>;

  thread_pool();
  thread_pool(logger& lgr, os_access const& os, char const* group_name,
              size_t num_workers = 1,
              size_t max_queue_len = std::numeric_limits<size_t>::max(),
              int niceness = 0);

  ~thread_pool();

  thread_pool(thread_pool&&) = default;
  thread_pool& operator=(thread_pool&&) = default;

  explicit operator bool() const { return static_cast<bool>(wg_); }

  bool add_job(job_type job);

  void stop();
  void wait();
  bool running() const;
  std::optional<std::chrono::nanoseconds> try_get_cpu_time() const;
  std::chrono::nanoseconds get_cpu_time(std::error_code& ec) const;

  internal::worker_group* operator->() { return wg_.get(); }
  internal::worker_group& get_worker_group() { return *wg_; }

 private:
  std::unique_ptr<internal::worker_group> wg_;
};

} // namespace dwarfs
