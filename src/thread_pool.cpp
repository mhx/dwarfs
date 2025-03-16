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

#include <dwarfs/thread_pool.h>

#include <dwarfs/internal/worker_group.h>

namespace dwarfs {

thread_pool::thread_pool() = default;
thread_pool::~thread_pool() = default;

thread_pool::thread_pool(logger& lgr, os_access const& os,
                         char const* group_name, size_t num_workers,
                         size_t max_queue_len, int niceness)
    : wg_{std::make_unique<internal::worker_group>(
          lgr, os, group_name, num_workers, max_queue_len, niceness)} {}

bool thread_pool::add_job(job_type job) { return wg_->add_job(std::move(job)); }

void thread_pool::stop() { wg_->stop(); }

void thread_pool::wait() { wg_->wait(); }

bool thread_pool::running() const { return wg_->running(); }

std::chrono::nanoseconds thread_pool::get_cpu_time(std::error_code& ec) const {
  return wg_->get_cpu_time(ec);
}

std::chrono::nanoseconds thread_pool::get_cpu_time() const {
  std::error_code ec;
  auto rv = get_cpu_time(ec);
  if (ec) {
    throw std::system_error(ec);
  }
  return rv;
}

std::optional<std::chrono::nanoseconds> thread_pool::try_get_cpu_time() const {
  return wg_->try_get_cpu_time();
}

} // namespace dwarfs
