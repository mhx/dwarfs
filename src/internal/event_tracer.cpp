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
#include <ostream>
#include <utility>
#include <vector>

#include <boost/container_hash/hash.hpp>

#include <dwarfs/util.h>

#include <dwarfs/internal/event_tracer.h>

#ifndef DWARFS_STACKTRACE_ENABLED
#error "event_tracer requires stack trace support to be enabled"
#endif

namespace dwarfs::internal {

event_tracer::event_tracer(event_tracer_config const& config)
    : event_tracer(config, [](std::size_t skip_frames, std::size_t max_depth) {
      return cpptrace::generate_raw_trace(skip_frames, max_depth);
    }) {}

event_tracer::event_tracer(event_tracer_config const& config,
                           capture_trace_fn capture_trace)
    : config_{config}
    , capture_trace_{std::move(capture_trace)} {}

auto event_tracer::make_fingerprint(cpptrace::raw_trace const& trace)
    -> fingerprint_type {
  return boost::hash_range(trace.frames.begin(), trace.frames.end());
}

bool event_tracer::same_trace(cpptrace::raw_trace const& lhs,
                              cpptrace::raw_trace const& rhs) {
  return lhs.frames == rhs.frames;
}

void event_tracer::trace(std::string_view event_name) const {
  auto raw_trace = capture_trace_(config_.skip_frames + 1, config_.max_depth);
  auto const fingerprint = make_fingerprint(raw_trace);

  events_.with_lock([&](auto& events) {
    auto& event = events[std::string(event_name)];
    ++event.total_count;

    auto& bucket = event.traces_by_fingerprint[fingerprint];
    auto const it = std::find_if(bucket.begin(), bucket.end(),
                                 [&](trace_entry const& entry) {
                                   return same_trace(entry.trace, raw_trace);
                                 });

    if (it == bucket.end()) {
      bucket.push_back(trace_entry{1, std::move(raw_trace)});
    } else {
      ++it->count;
    }
  });
}

void event_tracer::dump(std::ostream& os, bool color) const {
  events_.with_lock([&](auto& events) { dump_impl(events, os, color); });
}

void event_tracer::dump_impl(event_map const& events, std::ostream& os,
                             bool color) {
  if (events.empty()) {
    os << "event_tracer: no events recorded\n";
    return;
  }

  struct event_view {
    std::string const* name;
    event_entry const* event;
  };

  std::vector<event_view> ordered_events;
  ordered_events.reserve(events.size());

  for (auto const& [name, event] : events) {
    ordered_events.push_back(event_view{&name, &event});
  }

  std::ranges::sort(ordered_events,
                    [](event_view const& lhs, event_view const& rhs) {
                      if (lhs.event->total_count != rhs.event->total_count) {
                        return lhs.event->total_count > rhs.event->total_count;
                      }
                      return *lhs.name < *rhs.name;
                    });

  bool const dont_resolve_traces =
      getenv_is_enabled("DWARFS_EVENT_TRACER_DONT_RESOLVE");
  bool first_event = true;

  for (auto const& event_view : ordered_events) {
    if (!first_event) {
      os << '\n';
    }
    first_event = false;

    std::size_t unique_trace_count = 0;
    for (auto const& [fingerprint, bucket] :
         event_view.event->traces_by_fingerprint) {
      (void)fingerprint;
      unique_trace_count += bucket.size();
    }

    os << *event_view.name << ": " << event_view.event->total_count
       << " hit(s) across " << unique_trace_count << " unique backtrace(s)\n";

    struct trace_view {
      count_type count;
      cpptrace::raw_trace const* trace;
    };

    std::vector<trace_view> ordered_traces;
    ordered_traces.reserve(unique_trace_count);

    for (auto const& [fingerprint, bucket] :
         event_view.event->traces_by_fingerprint) {
      (void)fingerprint;
      for (auto const& entry : bucket) {
        ordered_traces.push_back(trace_view{entry.count, &entry.trace});
      }
    }

    std::ranges::sort(
        ordered_traces, [](trace_view const& lhs, trace_view const& rhs) {
          if (lhs.count != rhs.count) {
            return lhs.count > rhs.count;
          }
          if (lhs.trace->frames.size() != rhs.trace->frames.size()) {
            return lhs.trace->frames.size() > rhs.trace->frames.size();
          }
          return lhs.trace->frames < rhs.trace->frames;
        });

    for (auto const& trace_view : ordered_traces) {
      os << "  " << trace_view.count << " hit(s)\n";

      if (trace_view.trace->empty() || dont_resolve_traces) {
        os << "    <empty trace>\n";
      } else {
        trace_view.trace->resolve().print(os, color);
      }

      os << '\n';
    }
  }
}

} // namespace dwarfs::internal
