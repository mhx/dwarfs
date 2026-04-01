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

#include <algorithm>
#include <condition_variable>
#include <cstdint>
#include <ctime>
#include <exception>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include <fmt/format.h>

#include <dwarfs/portability/windows.h>

#include <dwarfs/error.h>
#include <dwarfs/logger.h>
#include <dwarfs/os_access.h>
#include <dwarfs/string.h>
#include <dwarfs/util.h>

#include <dwarfs/internal/thread_util.h>
#include <dwarfs/internal/worker_group.h>

namespace dwarfs::internal::detail {

namespace {

template <typename LoggerPolicy>
class worker_group_impl_ final : public worker_group_impl {
 public:
  worker_group_impl_(logger& lgr, os_access const& os,
                     std::string_view group_name,
                     worker_group_options const& options, state_factory sf)
      : LOG_PROXY_INIT(lgr)
      , os_{os}
      , max_queue_len_{std::max<size_t>(options.max_queue_len, 1)} {
    auto num_workers = options.num_workers;
    if (num_workers == 0) {
      num_workers = std::max(hardware_concurrency(), 1U);
    }

    if (group_name.empty()) {
      group_name = "worker";
    }

    auto thread_name_prefix = std::string(group_name);

    workers_.reserve(num_workers);

    for (size_t i = 0; i < num_workers; ++i) {
      workers_.emplace_back([this, niceness = options.niceness,
                             thread_name_prefix, i, state = sf(i)]() mutable {
        set_thread_name(fmt::format("{}{}", thread_name_prefix, i + 1));
        set_thread_niceness(niceness);
        do_work(*state, niceness > 10);
      });
    }

    check_set_affinity_from_environment(group_name);
  }

  worker_group_impl_(worker_group_impl_ const&) = delete;
  worker_group_impl_& operator=(worker_group_impl_ const&) = delete;

  ~worker_group_impl_() noexcept override {
    try {
      stop();
    } catch (...) {
      DWARFS_PANIC(
          fmt::format("exception thrown in worker group destructor: {}",
                      exception_str(std::current_exception())));
    }
  }

  void stop() override {
    {
      std::lock_guard lock(mx_);
      if (!running_) {
        return;
      }
      running_ = false;
    }

    jobs_available_.notify_all();
    queue_space_available_.notify_all();

    for (auto& worker : workers_) {
      if (worker.joinable()) {
        worker.join();
      }
    }
  }

  void wait() override {
    std::unique_lock lock(mx_);
    all_done_.wait(lock, [this] { return pending_ == 0; });
  }

  bool running() const override {
    std::lock_guard lock(mx_);
    return running_;
  }

  bool add_job(queued_job&& job) override {
    std::unique_lock lock(mx_);

    queue_space_available_.wait(
        lock, [this] { return !running_ || jobs_.size() < max_queue_len_; });

    if (!running_) {
      return false;
    }

    jobs_.emplace(std::move(job));
    ++pending_;

    lock.unlock();
    jobs_available_.notify_one();
    return true;
  }

  size_t size() const override { return workers_.size(); }

  std::chrono::nanoseconds get_cpu_time(std::error_code& ec) const override {
    ec.clear();

    std::lock_guard lock(mx_);
    std::chrono::nanoseconds total{};

    for (auto const& worker : workers_) {
      total += os_.thread_get_cpu_time(worker.get_id(), ec);
      if (ec) {
        total = std::chrono::nanoseconds{};
        break;
      }
    }

    return total;
  }

  std::optional<std::chrono::nanoseconds> try_get_cpu_time() const override {
    std::error_code ec;
    auto total = get_cpu_time(ec);
    return ec ? std::nullopt : std::make_optional(total);
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
  using jobs_t = std::queue<queued_job>;

  void check_set_affinity_from_environment(std::string_view group_name) {
    if (auto var = os_.getenv("DWARFS_WORKER_GROUP_AFFINITY")) {
      auto groups = split_to<std::vector<std::string_view>>(var.value(), ':');

      for (auto const& group : groups) {
        auto parts = split_to<std::vector<std::string_view>>(group, '=');

        if (parts.size() == 2 && parts[0] == group_name) {
          auto cpus = split_to<std::vector<int>>(parts[1], ',');
          set_affinity(cpus);
        }
      }
    }
  }

  void do_work(worker_context& state, bool is_background) {
    auto th = thread_helper{};

    for (;;) {
      std::optional<queued_job> job;

      {
        std::unique_lock lock(mx_);

        jobs_available_.wait(lock,
                             [this] { return !running_ || !jobs_.empty(); });

        if (jobs_.empty()) {
          break;
        }

        job.emplace(std::move(jobs_.front()));
        jobs_.pop();
      }

      queue_space_available_.notify_one();

      try {
        auto bg = th.background_scope(is_background);
        (*job)(state);
      } catch (...) {
        LOG_FATAL << "exception thrown in worker thread: "
                  << exception_str(std::current_exception());
      }

      bool notify_done = false;

      {
        std::lock_guard lock(mx_);
        notify_done = (--pending_ == 0);
      }

      if (notify_done) {
        all_done_.notify_all();
      }
    }
  }

  LOG_PROXY_DECL(LoggerPolicy);
  os_access const& os_;
  std::vector<std::thread> workers_;
  jobs_t jobs_;
  mutable std::mutex mx_;
  std::condition_variable jobs_available_;
  std::condition_variable queue_space_available_;
  std::condition_variable all_done_;
  bool running_{true};
  size_t pending_{0};
  size_t const max_queue_len_;
};

} // namespace

std::unique_ptr<worker_group_impl>
make_worker_group_impl(logger& lgr, os_access const& os,
                       std::string_view group_name,
                       worker_group_options const& options,
                       worker_group_impl::state_factory&& sf) {
  return make_unique_logging_object<worker_group_impl, worker_group_impl_,
                                    logger_policies>(lgr, os, group_name,
                                                     options, std::move(sf));
}

} // namespace dwarfs::internal::detail
