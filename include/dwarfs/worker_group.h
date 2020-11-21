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

#include <atomic>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <queue>
#include <thread>

#include <folly/Conv.h>
#include <folly/Function.h>
#include <folly/system/ThreadName.h>

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
      const char* group_name = nullptr, size_t num_workers = 1,
      size_t max_queue_len = std::numeric_limits<size_t>::max())
      : running_(true)
      , pending_(0)
      , max_queue_len_(max_queue_len) {
    if (num_workers < 1) {
      throw std::runtime_error("invalid number of worker threads");
    }
    if (!group_name) {
      group_name = "worker";
    }

    for (size_t i = 0; i < num_workers; ++i) {
      workers_.emplace_back([=, this] {
        folly::setThreadName(folly::to<std::string>(group_name, i + 1));
        do_work();
      });
    }
  }

  worker_group(const worker_group&) = delete;
  worker_group& operator=(const worker_group&) = delete;

  /**
   * Stop and destroy a worker group
   */
  ~worker_group() noexcept {
    try {
      stop();
    } catch (...) {
    }
  }

  /**
   * Stop a worker group
   */
  void stop() {
    if (running_) {
      {
        std::lock_guard<std::mutex> lock(mx_);
        running_ = false;
      }

      cond_.notify_all();

      for (auto& w : workers_) {
        w.join();
      }
    }
  }

  /**
   * Wait until all work has been done
   */
  void wait() {
    if (running_) {
      std::unique_lock<std::mutex> lock(mx_);
      wait_.wait(lock, [&] { return pending_ == 0; });
    }
  }

  /**
   * Check whether the worker group is still running
   */
  bool running() const { return running_; }

  /**
   * Add a new job to the worker group
   *
   * The new job will be dispatched to the first available worker thread.
   *
   * \param job             The job to add to the dispatcher.
   */
  bool add_job(job_t&& job) {
    if (running_) {
      {
        std::unique_lock<std::mutex> lock(mx_);
        queue_.wait(lock, [this] { return jobs_.size() < max_queue_len_; });
        jobs_.emplace(std::move(job));
        ++pending_;
      }

      cond_.notify_one();
    }

    return false;
  }

  /**
   * Return the number of worker threads
   *
   * \returns The number of worker threads.
   */
  size_t size() const { return workers_.size(); }

  /**
   * Return the number of worker threads
   *
   * \returns The number of worker threads.
   */
  size_t queue_size() const {
    std::lock_guard<std::mutex> lock(mx_);
    return jobs_.size();
  }

  /**
   * Return the number of queued jobs
   *
   * \returns The number of queued jobs.
   */
  size_t queued_jobs() const {
    std::lock_guard<std::mutex> lock(mx_);
    return jobs_.size();
  }

 private:
  using jobs_t = std::queue<job_t>;

  void do_work() {
    for (;;) {
      job_t job;

      {
        std::unique_lock<std::mutex> lock(mx_);

        while (jobs_.empty() && running_) {
          cond_.wait(lock);
        }

        if (jobs_.empty()) {
          if (running_)
            continue;
          else
            break;
        }

        job = std::move(jobs_.front());

        jobs_.pop();
      }

      job();

      {
        std::lock_guard<std::mutex> lock(mx_);
        pending_--;
      }

      wait_.notify_one();
      queue_.notify_one();
    }
  }

  std::vector<std::thread> workers_;
  jobs_t jobs_;
  std::condition_variable cond_;
  std::condition_variable queue_;
  std::condition_variable wait_;
  mutable std::mutex mx_;
  std::atomic<bool> running_;
  std::atomic<size_t> pending_;
  const size_t max_queue_len_;
};
} // namespace dwarfs
