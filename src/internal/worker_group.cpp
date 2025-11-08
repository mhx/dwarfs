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

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <exception>
#include <iostream>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

#include <fmt/format.h>

#include <folly/portability/Windows.h>
#include <folly/system/ThreadName.h>

#include <dwarfs/error.h>
#include <dwarfs/logger.h>
#include <dwarfs/os_access.h>
#include <dwarfs/string.h>
#include <dwarfs/util.h>

#include <dwarfs/internal/worker_group.h>

namespace dwarfs::internal {

namespace {

template <typename LoggerPolicy, typename Policy>
class basic_worker_group final : public worker_group::impl, private Policy {
 public:
  template <typename... Args>
  basic_worker_group(
      logger& lgr, os_access const& os, char const* group_name,
      size_t num_workers,
      std::function<std::unique_ptr<thread_state>(size_t)> thread_state_factory,
      size_t max_queue_len, int niceness [[maybe_unused]], Args&&... args)
      : Policy(std::forward<Args>(args)...)
      , LOG_PROXY_INIT(lgr)
      , os_{os}
      , running_(true)
      , pending_(0)
      , max_queue_len_(max_queue_len) {
    if (num_workers < 1) {
      num_workers = std::max(hardware_concurrency(), 1U);
    }

    if (!group_name) {
      group_name = "worker";
    }

    for (size_t i = 0; i < num_workers; ++i) {
      workers_.emplace_back(
          [this, niceness, group_name, i, state = thread_state_factory(i)] {
            folly::setThreadName(fmt::format("{}{}", group_name, i + 1));
            set_thread_niceness(niceness);
            do_work(*state, niceness > 10);
          });
    }

    check_set_affinity_from_enviroment(group_name);
  }

  basic_worker_group(basic_worker_group const&) = delete;
  basic_worker_group& operator=(basic_worker_group const&) = delete;

  /**
   * Stop and destroy a worker group
   */
  ~basic_worker_group() noexcept override {
    try {
      stop();
    } catch (...) {
      DWARFS_PANIC(
          fmt::format("exception thrown in worker group destructor: {}",
                      exception_str(std::current_exception())));
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
  bool add_job(std::any&& job) override {
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

  std::chrono::nanoseconds get_cpu_time(std::error_code& ec) const override {
    ec.clear();

    std::lock_guard lock(mx_);
    std::chrono::nanoseconds t{};

    for (auto const& w : workers_) {
      t += os_.thread_get_cpu_time(w.get_id(), ec);
      if (ec) {
        return {};
      }
    }

    return t;
  }

  std::optional<std::chrono::nanoseconds> try_get_cpu_time() const override {
    std::error_code ec;
    auto t = get_cpu_time(ec);
    return ec ? std::nullopt : std::make_optional(t);
  }

  bool set_affinity(std::vector<int> const& cpus) override {
    if (cpus.empty()) {
      return false;
    }

    std::lock_guard lock(mx_);

    for (auto const& worker : workers_) {
      std::error_code ec;
      os_.thread_set_affinity(worker.get_id(), cpus, ec);
      if (ec) {
        return false;
      }
    }

    return true;
  }

 private:
  using jobs_t = std::queue<std::any>;

  void check_set_affinity_from_enviroment(char const* group_name) {
    if (auto var = os_.getenv("DWARFS_WORKER_GROUP_AFFINITY")) {
      auto groups = split_to<std::vector<std::string_view>>(var.value(), ':');

      for (auto& group : groups) {
        auto parts = split_to<std::vector<std::string_view>>(group, '=');

        if (parts.size() == 2 && parts[0] == group_name) {
          auto cpus = split_to<std::vector<int>>(parts[1], ',');
          set_affinity(cpus);
        }
      }
    }
  }

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

  void do_work(thread_state& state, bool is_background [[maybe_unused]]) {
#ifdef _WIN32
    auto hthr = ::GetCurrentThread();
#endif
    for (;;) {
      std::any job;

      {
        std::unique_lock lock(mx_);

        while (jobs_.empty() && running_) {
          cond_.wait(lock);
        }

        if (jobs_.empty()) {
          if (running_) {
            continue;
          }
          break;
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
        try {
          state.apply(std::move(job));
        } catch (...) {
          LOG_FATAL << "exception thrown in worker thread: "
                    << exception_str(std::current_exception());
        }
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

  LOG_PROXY_DECL(LoggerPolicy);
  os_access const& os_;
  std::vector<std::thread> workers_;
  jobs_t jobs_;
  std::condition_variable cond_;
  std::condition_variable queue_;
  std::condition_variable wait_;
  mutable std::mutex mx_;
  std::atomic<bool> running_;
  std::atomic<size_t> pending_;
  size_t const max_queue_len_;
};

class no_policy {
 public:
  class task {
   public:
    explicit task(no_policy*) {}
  };
};

template <typename LoggerPolicy>
using default_worker_group = basic_worker_group<LoggerPolicy, no_policy>;

} // namespace

worker_group::worker_group(
    logger& lgr, os_access const& os, char const* group_name,
    size_t num_workers,
    std::function<std::unique_ptr<thread_state>(size_t)> thread_state_factory,
    size_t max_queue_len, int niceness)
    : impl_{make_unique_logging_object<impl, default_worker_group,
                                       logger_policies>(
          lgr, os, group_name, num_workers, thread_state_factory, max_queue_len,
          niceness)} {}

worker_group::worker_group(logger& lgr, os_access const& os,
                           char const* group_name, size_t num_workers,
                           size_t max_queue_len, int niceness)
    : worker_group(
          lgr, os, group_name, num_workers,
          [](size_t) { return std::make_unique<basic_thread_state<>>(); },
          max_queue_len, niceness) {}

} // namespace dwarfs::internal
