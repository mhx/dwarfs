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

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

#include <sys/time.h>
#include <unistd.h>

#include <pthread.h>

#include <folly/Conv.h>
#include <folly/system/ThreadName.h>

#include "dwarfs/error.h"
#include "dwarfs/semaphore.h"
#include "dwarfs/util.h"
#include "dwarfs/worker_group.h"

namespace dwarfs {

namespace {

pthread_t std_to_pthread_id(std::thread::id tid) {
  static_assert(std::is_same_v<pthread_t, std::thread::native_handle_type>);
  static_assert(sizeof(std::thread::id) ==
                sizeof(std::thread::native_handle_type));
  pthread_t id{0};
  std::memcpy(&id, &tid, sizeof(id));
  return id;
}

} // namespace

template <typename Policy>
class basic_worker_group final : public worker_group::impl, private Policy {
 public:
  template <typename... Args>
  basic_worker_group(const char* group_name, size_t num_workers,
                     size_t max_queue_len, int niceness, Args&&... args)
      : Policy(std::forward<Args>(args)...)
      , running_(true)
      , pending_(0)
      , max_queue_len_(max_queue_len) {
    if (num_workers < 1) {
      DWARFS_THROW(runtime_error, "invalid number of worker threads");
    }

    if (!group_name) {
      group_name = "worker";
    }

    for (size_t i = 0; i < num_workers; ++i) {
      workers_.emplace_back([=] {
        folly::setThreadName(folly::to<std::string>(group_name, i + 1));
        [[maybe_unused]] auto rv = ::nice(niceness);
        do_work();
      });
    }
  }

  basic_worker_group(const basic_worker_group&) = delete;
  basic_worker_group& operator=(const basic_worker_group&) = delete;

  /**
   * Stop and destroy a worker group
   */
  ~basic_worker_group() noexcept override {
    try {
      stop();
    } catch (...) {
    }
  }

  /**
   * Stop a worker group
   */
  void stop() override {
    if (running_) {
      {
        std::lock_guard lock(mx_);
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
  void wait() override {
    if (running_) {
      std::unique_lock lock(mx_);
      wait_.wait(lock, [&] { return pending_ == 0; });
    }
  }

  /**
   * Check whether the worker group is still running
   */
  bool running() const override { return running_; }

  /**
   * Add a new job to the worker group
   *
   * The new job will be dispatched to the first available worker thread.
   *
   * \param job             The job to add to the dispatcher.
   */
  bool add_job(worker_group::job_t&& job) override {
    if (running_) {
      {
        std::unique_lock lock(mx_);
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
  size_t size() const override { return workers_.size(); }

  /**
   * Return the number of queued jobs
   *
   * \returns The number of queued jobs.
   */
  size_t queue_size() const override {
    std::lock_guard lock(mx_);
    return jobs_.size();
  }

  double get_cpu_time() const override {
    std::lock_guard lock(mx_);
    double t = 0.0;

    for (auto const& w : workers_) {
      ::clockid_t cid;
      struct ::timespec ts;
      if (::pthread_getcpuclockid(std_to_pthread_id(w.get_id()), &cid) == 0 &&
          ::clock_gettime(cid, &ts) == 0) {
        t += ts.tv_sec + 1e-9 * ts.tv_nsec;
      }
    }

    return t;
  }

 private:
  using jobs_t = std::queue<worker_group::job_t>;

  void do_work() {
    for (;;) {
      worker_group::job_t job;

      {
        std::unique_lock lock(mx_);

        while (jobs_.empty() && running_) {
          cond_.wait(lock);
        }

        if (jobs_.empty()) {
          if (running_) {
            continue;
          } else {
            break;
          }
        }

        job = std::move(jobs_.front());

        jobs_.pop();
      }

      {
        typename Policy::task task(this);
        job();
      }

      {
        std::lock_guard lock(mx_);
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

class no_policy {
 public:
  class task {
   public:
    explicit task(no_policy*) {}
  };
};

worker_group::worker_group(const char* group_name, size_t num_workers,
                           size_t max_queue_len, int niceness)
    : impl_{std::make_unique<basic_worker_group<no_policy>>(
          group_name, num_workers, max_queue_len, niceness)} {}

} // namespace dwarfs
