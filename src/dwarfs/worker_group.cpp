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

#include <iostream> // TODO: remove

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>

#include <sys/resource.h>
#include <sys/time.h>
#include <time.h>

#include <folly/Conv.h>
#include <folly/system/ThreadName.h>

#include "dwarfs/semaphore.h"
#include "dwarfs/worker_group.h"

namespace dwarfs {

template <typename Policy>
class basic_worker_group : public worker_group::impl, private Policy {
 public:
  template <typename... Args>
  basic_worker_group(const char* group_name, size_t num_workers,
                     size_t max_queue_len, Args&&... args)
      : Policy(std::forward<Args>(args)...)
      , running_(true)
      , pending_(0)
      , max_queue_len_(max_queue_len) {
    if (num_workers < 1) {
      throw std::runtime_error("invalid number of worker threads");
    }
    if (!group_name) {
      group_name = "worker";
    }

    for (size_t i = 0; i < num_workers; ++i) {
      workers_.emplace_back([=] {
        folly::setThreadName(folly::to<std::string>(group_name, i + 1));
        do_work();
      });
    }
  }

  basic_worker_group(const basic_worker_group&) = delete;
  basic_worker_group& operator=(const basic_worker_group&) = delete;

  /**
   * Stop and destroy a worker group
   */
  ~basic_worker_group() noexcept {
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
  bool add_job(worker_group::job_t&& job) {
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
   * Return the number of queued jobs
   *
   * \returns The number of queued jobs.
   */
  size_t queue_size() const {
    std::lock_guard<std::mutex> lock(mx_);
    return jobs_.size();
  }

 private:
  using jobs_t = std::queue<worker_group::job_t>;

  void do_work() {
    for (;;) {
      worker_group::job_t job;

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

      {
        typename Policy::task task(this);
        job();
      }

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

class no_policy {
 public:
  class task {
   public:
    task(no_policy*) {}
  };
};

class load_adaptive_policy {
 public:
  class task {
   public:
    task(load_adaptive_policy* policy)
        : policy_(policy) {
      policy_->start_task();

      struct rusage usage;
      getrusage(RUSAGE_THREAD, &usage);
      utime_ = usage.ru_utime;
      stime_ = usage.ru_stime;
      clock_gettime(CLOCK_MONOTONIC, &wall_);
    }

    ~task();

   private:
    load_adaptive_policy* policy_;
    struct timespec wall_;
    struct timeval utime_, stime_;
  };

  load_adaptive_policy(size_t workers)
      : sem_(workers)
      , max_throttled_(workers - 1) {}

  void start_task() { sem_.acquire(); }

  void stop_task(uint64_t wall_ns, uint64_t cpu_ns);

 private:
  semaphore sem_;
  int max_throttled_;
  std::mutex mx_;
  uint64_t wall_ns_, cpu_ns_;
  int throttled_;
};

load_adaptive_policy::task::~task() {
  struct rusage usage;
  getrusage(RUSAGE_THREAD, &usage);
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);

  uint64_t wall_ns = UINT64_C(1000000000) * (now.tv_sec - wall_.tv_sec);
  wall_ns += now.tv_nsec;
  wall_ns -= wall_.tv_nsec;

  uint64_t cpu_ns =
      UINT64_C(1000000000) * (usage.ru_utime.tv_sec + usage.ru_stime.tv_sec -
                              (utime_.tv_sec + stime_.tv_sec));
  cpu_ns += UINT64_C(1000) * (usage.ru_utime.tv_usec + usage.ru_stime.tv_usec);
  cpu_ns -= UINT64_C(1000) * (utime_.tv_usec + stime_.tv_usec);

  policy_->stop_task(wall_ns, cpu_ns);
}

void load_adaptive_policy::stop_task(uint64_t wall_ns, uint64_t cpu_ns) {
  int adjust = 0;

  {
    std::unique_lock<std::mutex> lock(mx_);

    wall_ns_ += wall_ns;
    cpu_ns_ += cpu_ns;

    if (wall_ns_ >= 1000000000) {
      auto load = float(cpu_ns_) / float(wall_ns_);
      if (load > 0.75f) {
        if (throttled_ > 0) {
          --throttled_;
          adjust = 1;
        }
      } else if (load < 0.25f) {
        if (throttled_ < max_throttled_) {
          ++throttled_;
          adjust = -1;
        }
      }
      wall_ns_ = 0;
      cpu_ns_ = 0;
    }
  }

  if (adjust < 0) {
    return;
  }

  if (adjust > 0) {
    sem_.release();
  }

  sem_.release();
}

worker_group::worker_group(const char* group_name, size_t num_workers,
                           size_t max_queue_len)
    : impl_{std::make_unique<basic_worker_group<no_policy>>(
          group_name, num_workers, max_queue_len)} {}

worker_group::worker_group(load_adaptive_tag, const char* group_name,
                           size_t max_num_workers, size_t max_queue_len)
    : impl_{std::make_unique<basic_worker_group<load_adaptive_policy>>(
          group_name, max_num_workers, max_queue_len, max_num_workers)} {}

} // namespace dwarfs
