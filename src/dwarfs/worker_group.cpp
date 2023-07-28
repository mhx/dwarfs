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

#include <folly/Conv.h>
#include <folly/portability/PThread.h>
#include <folly/portability/Windows.h>
#include <folly/system/ThreadName.h>

#include "dwarfs/error.h"
#include "dwarfs/semaphore.h"
#include "dwarfs/util.h"
#include "dwarfs/worker_group.h"

namespace dwarfs {

namespace {

#ifdef _WIN32

double get_thread_cpu_time(std::thread const& t) {
  static_assert(sizeof(std::thread::id) == sizeof(DWORD),
                "Win32 thread id type mismatch");
  auto tid = t.get_id();
  DWORD id;
  std::memcpy(&id, &tid, sizeof(id));
  HANDLE h = ::OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, id);
  FILETIME t_create, t_exit, t_sys, t_user;
  if (::GetThreadTimes(h, &t_create, &t_exit, &t_sys, &t_user)) {
    uint64_t sys = (static_cast<uint64_t>(t_sys.dwHighDateTime) << 32) +
                   t_sys.dwLowDateTime;
    uint64_t user = (static_cast<uint64_t>(t_user.dwHighDateTime) << 32) +
                    t_user.dwLowDateTime;
    return 1e-7 * (sys + user);
  }
  throw std::runtime_error("get_thread_cpu_time");
}

#else

pthread_t std_to_pthread_id(std::thread::id tid) {
  static_assert(std::is_same_v<pthread_t, std::thread::native_handle_type>);
  static_assert(sizeof(std::thread::id) ==
                sizeof(std::thread::native_handle_type));
  pthread_t id{0};
  std::memcpy(&id, &tid, sizeof(id));
  return id;
}

double get_thread_cpu_time(std::thread const& t) {
  ::clockid_t cid;
  struct ::timespec ts;
  if (::pthread_getcpuclockid(std_to_pthread_id(t.get_id()), &cid) == 0 &&
      ::clock_gettime(cid, &ts) == 0) {
    return ts.tv_sec + 1e-9 * ts.tv_nsec;
  }
  throw std::runtime_error("get_thread_cpu_time");
}

#endif

} // namespace

template <typename Policy>
class basic_worker_group final : public worker_group::impl, private Policy {
 public:
  template <typename... Args>
  basic_worker_group(const char* group_name, size_t num_workers,
                     size_t max_queue_len, int niceness [[maybe_unused]],
                     Args&&... args)
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
      workers_.emplace_back([this, niceness, group_name, i] {
        folly::setThreadName(folly::to<std::string>(group_name, i + 1));
        set_thread_niceness(niceness);
        do_work(niceness > 10);
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

      return true;
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

    // TODO:
    // return workers_ | std::views::transform(get_thread_cpu_time) |
    // std::ranges::accumulate;
    for (auto const& w : workers_) {
      t += get_thread_cpu_time(w);
    }

    return t;
  }

 private:
  using jobs_t = std::queue<worker_group::job_t>;

  // TODO: move out of this class
  static void set_thread_niceness(int niceness) {
    if (niceness > 0) {
#ifdef _WIN32
      auto hthr = ::GetCurrentThread();
      int priority =
          niceness > 5 ? THREAD_PRIORITY_LOWEST : THREAD_PRIORITY_BELOW_NORMAL;
      ::SetThreadPriority(hthr, priority);
#else
      // XXX:
      // According to POSIX, the nice value is a per-process setting. However,
      // under the current Linux/NPTL implementation of POSIX threads, the nice
      // value is a per-thread attribute: different threads in the same process
      // can have different nice values. Portable applications should avoid
      // relying on the Linux behavior, which may be made standards conformant
      // in the future.
      auto rv [[maybe_unused]] = ::nice(niceness);
#endif
    }
  }

  void do_work(bool is_background [[maybe_unused]]) {
#ifdef _WIN32
    auto hthr = ::GetCurrentThread();
#endif
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
#ifdef _WIN32
        if (is_background) {
          ::SetThreadPriority(hthr, THREAD_MODE_BACKGROUND_BEGIN);
        }
#endif
        job();
#ifdef _WIN32
        if (is_background) {
          ::SetThreadPriority(hthr, THREAD_MODE_BACKGROUND_END);
        }
#endif
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
