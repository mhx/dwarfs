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

#include <algorithm>
#include <atomic>
#include <deque>
#include <mutex>
#include <string_view>

#ifdef _WIN32
#include <folly/portability/Windows.h>
#else
#include <sys/time.h>
#endif

#include <folly/lang/Bits.h>
#include <folly/stats/Histogram.h>

#include "dwarfs/performance_monitor.h"
#include "dwarfs/util.h"

namespace dwarfs {

namespace {

class single_timer {
 public:
  static constexpr uint64_t const histogram_interval = 1;

  single_timer(std::string const& name_space, std::string const& name)
      : log_hist_{1, 0, 64}
      , namespace_{name_space}
      , name_{name} {
    total_time_.store(0);
  }

  void add_sample(uint64_t elapsed) {
    total_time_.fetch_add(elapsed);

    if ((samples_.fetch_add(1) % histogram_interval) == 0) {
      auto log_time = folly::findLastSet(elapsed);
      std::lock_guard lock(log_hist_mutex_);
      log_hist_.addValue(log_time);
    }
  }

  std::string_view get_namespace() const { return namespace_; }

  std::string_view name() const { return name_; }

  uint64_t total_latency() const { return total_time_.load(); }

  void summarize(std::ostream& os, double timebase) const {
    if (samples_.load() == 0) {
      return;
    }

    size_t log_p50, log_p90, log_p99;
    {
      std::lock_guard lock(log_hist_mutex_);
      log_p50 = log_hist_.getPercentileEstimate(0.5);
      log_p90 = log_hist_.getPercentileEstimate(0.9);
      log_p99 = log_hist_.getPercentileEstimate(0.99);
    }
    auto samples = samples_.load();
    auto total_time = total_time_.load();

    auto tot = timebase * total_time;
    auto avg = tot / samples;
    auto p50 = timebase * (UINT64_C(1) << log_p50);
    auto p90 = timebase * (UINT64_C(1) << log_p90);
    auto p99 = timebase * (UINT64_C(1) << log_p99);

    os << "[" << namespace_ << "." << name_ << "]\n";
    os << "      samples: " << samples << "\n";
    os << "      overall: " << time_with_unit(tot) << "\n";
    os << "  avg latency: " << time_with_unit(avg) << "\n";
    os << "  p50 latency: " << time_with_unit(p50) << "\n";
    os << "  p90 latency: " << time_with_unit(p90) << "\n";
    os << "  p99 latency: " << time_with_unit(p99) << "\n\n";
  }

 private:
  std::atomic<uint64_t> samples_;
  std::atomic<uint64_t> total_time_;
  folly::Histogram<size_t> log_hist_;
  std::mutex mutable log_hist_mutex_;
  std::string const namespace_;
  std::string const name_;
};

} // namespace

class performance_monitor_impl : public performance_monitor {
 public:
  using timer_id = performance_monitor::timer_id;
  using time_type = performance_monitor::time_type;

  explicit performance_monitor_impl(
      std::unordered_set<std::string> enabled_namespaces)
      : timebase_{get_timebase()}
      , enabled_namespaces_{std::move(enabled_namespaces)} {}

  timer_id
  setup_timer(std::string const& ns, std::string const& name) const override {
    std::lock_guard lock(timers_mx_);
    timer_id rv = timers_.size();
    timers_.emplace_back(ns, name);
    return rv;
  }

  time_type now() const override {
#ifdef _WIN32
    ::LARGE_INTEGER ticks;
    ::QueryPerformanceCounter(&ticks);
    return ticks.QuadPart;
#else
    struct timespec ts;
    ::clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return UINT64_C(1'000'000'000) * ts.tv_sec + ts.tv_nsec;
#endif
  }

  void add_sample(timer_id id, time_type start) const override {
    auto elapsed = now() - start;
    // No need to acquire the mutex here as existing timers
    // never move in the deque.
    timers_[id].add_sample(elapsed);
  }

  void summarize(std::ostream& os) const override {
    int count;

    {
      std::lock_guard lock(timers_mx_);
      count = timers_.size();
    }

    std::vector<std::tuple<std::string_view, uint64_t, int>> ts;

    for (int i = 0; i < count; ++i) {
      auto& t = timers_[i];
      ts.emplace_back(t.get_namespace(), t.total_latency(), i);
    }

    std::sort(ts.begin(), ts.end(), [](auto const& a, auto const& b) {
      return std::get<0>(a) < std::get<0>(b) ||
             (std::get<0>(a) == std::get<0>(b) &&
              std::get<1>(a) > std::get<1>(b));
    });

    for (auto const& t : ts) {
      timers_[std::get<2>(t)].summarize(os, timebase_);
    }
  }

  bool is_enabled(std::string const& ns) const override {
    return enabled_namespaces_.find(ns) != enabled_namespaces_.end();
  }

 private:
  static double get_timebase() {
#ifdef _WIN32
    ::LARGE_INTEGER freq;
    ::QueryPerformanceFrequency(&freq);
    return 1.0 / freq.QuadPart;
#else
    return 1e-9;
#endif
  }

  std::deque<single_timer> mutable timers_;
  std::mutex mutable timers_mx_;
  double const timebase_;
  std::unordered_set<std::string> const enabled_namespaces_;
};

performance_monitor_proxy::performance_monitor_proxy(
    std::shared_ptr<performance_monitor const> mon,
    std::string const& mon_namespace)
    : mon_{mon && mon->is_enabled(mon_namespace) ? std::move(mon) : nullptr}
    , namespace_{mon_namespace} {}

std::unique_ptr<performance_monitor> performance_monitor::create(
    std::unordered_set<std::string> const& enabled_namespaces) {
  return enabled_namespaces.empty()
             ? nullptr
             : std::make_unique<performance_monitor_impl>(
                   std::move(enabled_namespaces));
}

} // namespace dwarfs
