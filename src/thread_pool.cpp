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

std::optional<std::chrono::nanoseconds> thread_pool::try_get_cpu_time() const {
  return wg_->try_get_cpu_time();
}

} // namespace dwarfs
