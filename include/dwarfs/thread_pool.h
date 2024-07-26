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

#include <chrono>
#include <limits>
#include <memory>
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
  thread_pool();
  thread_pool(logger& lgr, os_access const& os, const char* group_name,
              size_t num_workers = 1,
              size_t max_queue_len = std::numeric_limits<size_t>::max(),
              int niceness = 0);

  ~thread_pool();

  thread_pool(thread_pool&&) = default;
  thread_pool& operator=(thread_pool&&) = default;

  explicit operator bool() const { return static_cast<bool>(wg_); }

  void stop();
  void wait();
  bool running() const;
  std::chrono::nanoseconds get_cpu_time() const;
  std::chrono::nanoseconds get_cpu_time(std::error_code& ec) const;

  internal::worker_group* operator->() { return wg_.get(); }
  internal::worker_group& get_worker_group() { return *wg_; }

 private:
  std::unique_ptr<internal::worker_group> wg_;
};

} // namespace dwarfs
