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
#include <optional>
#include <thread>

#include <folly/system/ThreadName.h>

#include <dwarfs/reader/internal/periodic_executor.h>

namespace dwarfs::reader::internal {

namespace {

class periodic_executor_ final : public periodic_executor::impl {
 public:
  periodic_executor_(std::mutex& mx, std::chrono::nanoseconds period,
                     std::string_view name, std::function<void()> func)
      : mx_{mx}
      , period_{period}
      , name_{name}
      , func_{std::move(func)} {}

  ~periodic_executor_() override { stop(); }

  void start() const override {
    std::lock_guard lock(mx_);
    if (!running_.load()) {
      running_.store(true);
      thread_.emplace(&periodic_executor_::run, this);
    }
  }

  void stop() const override {
    std::unique_lock lock(mx_);
    if (running_.load()) {
      running_.store(false);
      lock.unlock();
      cv_.notify_all();
      thread_->join();
      thread_.reset();
    }
  }

  bool running() const override { return running_.load(); }

  void set_period(std::chrono::nanoseconds period) const override {
    {
      std::lock_guard lock(mx_);
      period_ = period;
    }

    if (running_.load()) {
      cv_.notify_all();
    }
  }

 private:
  void run() const {
    folly::setThreadName(name_);
    std::unique_lock lock(mx_);
    while (running_.load()) {
      if (cv_.wait_for(lock, period_) == std::cv_status::timeout) {
        func_();
      }
    }
  }

  std::mutex& mx_;
  std::condition_variable mutable cv_;
  std::atomic<bool> mutable running_{false};
  std::optional<std::thread> mutable thread_;
  std::chrono::nanoseconds mutable period_;
  std::string const name_;
  std::function<void()> func_;
};

} // namespace

periodic_executor::periodic_executor(std::mutex& mx,
                                     std::chrono::nanoseconds period,
                                     std::string_view name,
                                     std::function<void()> func)
    : impl_{std::make_unique<periodic_executor_>(mx, period, name,
                                                 std::move(func))} {}

} // namespace dwarfs::reader::internal
