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

#include <folly/portability/SysResource.h>
#include <folly/portability/SysTime.h>
#include <folly/portability/Unistd.h>

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

#include <pthread.h>

#include <folly/Conv.h>
#include <folly/system/ThreadName.h>

#include "dwarfs/error.h"
#include "dwarfs/semaphore.h"
#include "dwarfs/util.h"
#include "dwarfs/worker_group.h"

#if __MACH__
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach/thread_info.h>
#include <mach/mach_vm.h>
#include <mach/mach_init.h>
#include <mach/mach_port.h>
#include <mach/thread_act.h>
#endif

namespace dwarfs {

namespace {

static int getrusage_thread(struct rusage *rusage)
{
    int ret = -1;
#if __MACH__
    thread_basic_info_data_t info;
    memset(&info, 0, sizeof(info));
    mach_msg_type_number_t info_count = THREAD_BASIC_INFO_COUNT;
    kern_return_t kern_err;

    mach_port_t port = mach_thread_self();
    kern_err = thread_info(port,
                           THREAD_BASIC_INFO,
                           (thread_info_t)&info,
                           &info_count);
    mach_port_deallocate(mach_task_self(), port);

    if (kern_err == KERN_SUCCESS) {
        memset(rusage, 0, sizeof(struct rusage));
        rusage->ru_utime.tv_sec = info.user_time.seconds;
        rusage->ru_utime.tv_usec = info.user_time.microseconds;
        rusage->ru_stime.tv_sec = info.system_time.seconds;
        rusage->ru_stime.tv_usec = info.system_time.microseconds;
        ret = 0;
    } else {
        errno = EINVAL;
    }
#else
    ret = getrusage(RUSAGE_THREAD, rusage);
#endif
    return ret;
}

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
                     size_t max_queue_len,
#ifndef _WIN32
                     int niceness,
#else
                     int nPriority,
#endif
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
      workers_.emplace_back([=] {
        folly::setThreadName(folly::to<std::string>(group_name, i + 1));
#ifndef _WIN32
        [[maybe_unused]] auto rv = nice(niceness);
#else
        [[maybe_unused]] auto rv = SetThreadPriority(GetCurrentThread(), nPriority);
#endif
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
#if __MACH__
//  https://www.programcreek.com/cpp/?CodeExample=thread+cpu+usage
      mach_msg_type_number_t count;
      thread_basic_info_data_t info;
      count = THREAD_BASIC_INFO_COUNT;
      mach_port_t thread = pthread_mach_thread_np(std_to_pthread_id(w.get_id()));
      if (::thread_info(thread, THREAD_BASIC_INFO, (thread_info_t)&info, &count) == KERN_SUCCESS) {
        t += info.user_time.seconds + info.user_time.microseconds * 1e-6;
        t += info.system_time.seconds + info.system_time.microseconds * 1e-6;
      }
#elif defined(_WIN32)
      FILETIME CreationTime, ExitTime, KernelTime, UserTime;
// pthread_gethandle is MINGW private extension
// MSVC provides pthread_getw32threadid_np [it is just a note in case anyone decides to support MSVC ]
      HANDLE hThread = pthread_gethandle(std_to_pthread_id(w.get_id()));
      BOOL r = GetThreadTimes(hThread, &CreationTime, &ExitTime, &KernelTime, &UserTime);
      if (r) {
        t = UserTime.dwLowDateTime * 1e-7 + KernelTime.dwLowDateTime * 1e-7;
      }
// We do nothing on error, just leave time equal to 0
// Also note that GetThreadTimes is not really reliable
#else
      ::clockid_t cid;
      struct ::timespec ts;
      if (::pthread_getcpuclockid(std_to_pthread_id(w.get_id()), &cid) == 0 &&
          ::clock_gettime(cid, &ts) == 0) {
        t += ts.tv_sec + 1e-9 * ts.tv_nsec;
      }
#endif
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

class load_adaptive_policy {
 public:
  class task {
   public:
    explicit task(load_adaptive_policy* policy)
        : policy_(policy) {
      policy_->start_task();

      struct rusage usage;
      getrusage_thread(&usage);
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

  explicit load_adaptive_policy(size_t workers)
      : sem_(workers)
      , max_throttled_(static_cast<int>(workers) - 1) {}

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
  getrusage_thread(&usage);
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
    std::unique_lock lock(mx_);

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
                           size_t max_queue_len, int niceness)
    : impl_{std::make_unique<basic_worker_group<no_policy>>(
          group_name, num_workers, max_queue_len, niceness)} {}

worker_group::worker_group(load_adaptive_tag, const char* group_name,
                           size_t max_num_workers, size_t max_queue_len,
                           int niceness)
    : impl_{std::make_unique<basic_worker_group<load_adaptive_policy>>(
          group_name, max_num_workers, max_queue_len, niceness,
          max_num_workers)} {}

} // namespace dwarfs
