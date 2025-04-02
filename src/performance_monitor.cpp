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
#include <atomic>
#include <cassert>
#include <deque>
#include <mutex>
#include <string_view>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <folly/portability/Windows.h>
#else
#include <sys/time.h>
#endif

#include <folly/lang/Bits.h>
#include <folly/portability/Unistd.h>
#include <folly/stats/Histogram.h>

#include <fmt/format.h>
#if FMT_VERSION >= 110000
#include <fmt/ranges.h>
#endif

#include <range/v3/view/enumerate.hpp>

#include <dwarfs/file_access.h>
#include <dwarfs/performance_monitor.h>
#include <dwarfs/util.h>

namespace dwarfs {

namespace internal {

namespace {

class single_timer {
 public:
  static constexpr uint64_t const histogram_interval = 1;

  single_timer(std::string const& name_space, std::string const& name,
               std::initializer_list<std::string_view> context)
      : log_hist_{1, 0, 64}
      , namespace_{name_space}
      , name_{name}
      , context_{context.begin(), context.end()} {
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

  std::span<std::string const> context() const { return context_; }

 private:
  std::atomic<uint64_t> samples_{};
  std::atomic<uint64_t> total_time_{};
  folly::Histogram<size_t> log_hist_;
  std::mutex mutable log_hist_mutex_;
  std::string const namespace_;
  std::string const name_;
  std::vector<std::string> const context_;
};

} // namespace

class performance_monitor_impl final : public performance_monitor {
 public:
  using timer_id = performance_monitor::timer_id;
  using time_type = performance_monitor::time_type;

  struct trace_event {
    trace_event(timer_id id, time_type start, time_type end,
                std::span<uint64_t const> ctx)
        : id{id}
        , start{start}
        , end{end}
        , context{ctx.begin(), ctx.end()} {}

    // TODO: workaround for older boost small_vector
    std::span<uint64_t const> context_span() const {
      return std::span{context.data(), context.size()};
    }

    timer_id id;
    time_type start;
    time_type end;
    small_vector<uint64_t, kNumInlineContext> context;
  };

  explicit performance_monitor_impl(
      std::unordered_set<std::string> enabled_namespaces,
      std::shared_ptr<file_access const> fa,
      std::optional<std::filesystem::path> trace_file)
      : timebase_{get_timebase()}
      , enabled_namespaces_{std::move(enabled_namespaces)}
      , start_time_{now()}
      , trace_file_{std::move(trace_file)}
      , fa_{std::move(fa)} {}

  ~performance_monitor_impl() override {
    if (trace_file_) {
      write_trace_events(*trace_file_);
    }
  }

  timer_id
  setup_timer(std::string const& ns, std::string const& name,
              std::initializer_list<std::string_view> context) const override {
    std::lock_guard lock(timers_mx_);
    timer_id rv = timers_.size();
    timers_.emplace_back(ns, name, context);
    return rv;
  }

  time_type now() const override {
#ifdef _WIN32
    ::LARGE_INTEGER ticks;
    ::QueryPerformanceCounter(&ticks);
    return ticks.QuadPart;
#else
    struct timespec ts;
    auto rv [[maybe_unused]] = ::clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    assert(rv == 0);
    return UINT64_C(1'000'000'000) * ts.tv_sec + ts.tv_nsec;
#endif
  }

  void add_sample(timer_id id, time_type start,
                  std::span<uint64_t const> context) const override {
    auto end = now();
    // No need to acquire the mutex here as existing timers
    // never move in the deque.
    auto& t = timers_[id];
    t.add_sample(end - start);

    if (trace_file_) {
      std::vector<trace_event>* events;

      {
        std::lock_guard lock(events_mx_);
        auto& evp = trace_events_[std::this_thread::get_id()];
        if (!evp) {
          evp = std::make_unique<std::vector<trace_event>>();
        }
        events = evp.get();
      }

      events->emplace_back(id, start, end, context);
    }
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

    std::ranges::sort(ts, [](auto const& a, auto const& b) {
      return std::get<0>(a) < std::get<0>(b) ||
             (std::get<0>(a) == std::get<0>(b) &&
              std::get<1>(a) > std::get<1>(b));
    });

    for (auto const& t : ts) {
      timers_[std::get<2>(t)].summarize(os, timebase_);
    }
  }

  bool is_enabled(std::string const& ns) const override {
    return enabled_namespaces_.contains(ns);
  }

  bool wants_context() const override { return trace_file_.has_value(); }

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

  struct json_trace_event {
    json_trace_event(timer_id id, int tid, char ph, time_type ts,
                     std::span<uint64_t const> ctxt = {})
        : id{id}
        , tid{tid}
        , ph{ph}
        , ts{ts}
        , args{ctxt.begin(), ctxt.end()} {}

    timer_id id;
    int tid;
    char ph;
    time_type ts;
    std::vector<uint64_t> args;
  };

  void write_trace_events(std::filesystem::path const& path) const {
    std::error_code ec;
    auto output = fa_->open_output(path, ec);

    if (ec) {
      return; // Meh.
    }

    std::vector<json_trace_event> events;
    std::unordered_map<std::thread::id, int> thread_ids;

    {
      std::lock_guard lock(events_mx_);
      int max_tid{0};

      for (auto const& [tid, evs] : trace_events_) {
        auto it = thread_ids.find(tid);
        if (it == thread_ids.end()) {
          it = thread_ids.emplace(tid, ++max_tid).first;
        }

        for (auto const& ev : *evs) {
          events.emplace_back(ev.id, it->second, 'B', ev.start - start_time_,
                              ev.context_span());
          events.emplace_back(ev.id, it->second, 'E', ev.end - start_time_);
        }
      }
    }

    std::ranges::sort(events,
                      [](auto const& a, auto const& b) { return a.ts < b.ts; });

    bool first = true;
    auto const pid = ::getpid();

    auto& os = output->os();

    os << "[\n";

    std::lock_guard lock(timers_mx_);

    for (auto const& ev : events) {
      if (!first) {
        os << ",\n";
      }
      first = false;

      auto& t = timers_[ev.id];
      std::string name;

      if (!ev.args.empty()) {
        name = fmt::format("{}({})", t.name(), fmt::join(ev.args, ", "));
      } else {
        name = t.name();
      }

      os << fmt::format("  {{\n    \"name\": \"{}\",\n    \"cat\": \"{}\",\n",
                        name, t.get_namespace());
      os << fmt::format("    \"ph\": \"{}\",\n    \"ts\": {:.3f},\n", ev.ph,
                        1e6 * ev.ts * timebase_);
      os << fmt::format("    \"pid\": {},\n    \"tid\": {}", pid, ev.tid);
      if (!ev.args.empty()) {
        auto ctx_names = t.context();
        os << ",\n    \"args\": {";
        for (auto [i, arg] : ranges::views::enumerate(ev.args)) {
          if (i > 0) {
            os << ", ";
          }
          std::string arg_name;
          if (i < ctx_names.size()) {
            arg_name = ctx_names[i];
          } else {
            arg_name = fmt::format("arg{}", i);
          }
          os << fmt::format("\"{}\": {}", arg_name, arg);
        }
        os << "}";
      }
      os << "\n  }";
    }

    os << "\n]\n";

    output->close();
  }

  std::deque<single_timer> mutable timers_;
  std::mutex mutable timers_mx_;
  double const timebase_;
  std::unordered_set<std::string> const enabled_namespaces_;
  time_type const start_time_;
  std::optional<std::filesystem::path> const trace_file_;
  std::mutex mutable events_mx_;
  std::unordered_map<
      std::thread::id,
      std::unique_ptr<std::vector<trace_event>>> mutable trace_events_;
  std::shared_ptr<file_access const> fa_;
};

} // namespace internal

performance_monitor_proxy::performance_monitor_proxy(
    std::shared_ptr<performance_monitor const> mon,
    std::string const& proxy_namespace)
    : mon_{mon && mon->is_enabled(proxy_namespace) ? std::move(mon) : nullptr}
    , namespace_{proxy_namespace} {}

std::unique_ptr<performance_monitor>
performance_monitor::create(std::unordered_set<std::string> enabled_namespaces,
                            std::shared_ptr<file_access const> fa,
                            std::optional<std::filesystem::path> trace_file) {
  return enabled_namespaces.empty()
             ? nullptr
             : std::make_unique<internal::performance_monitor_impl>(
                   std::move(enabled_namespaces), std::move(fa),
                   std::move(trace_file));
}

} // namespace dwarfs
