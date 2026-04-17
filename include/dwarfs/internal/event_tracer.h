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

#pragma once

#include <dwarfs/config.h>

#ifdef DWARFS_STACKTRACE_ENABLED

#include <cstddef>
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <cpptrace/basic.hpp>

#include <parallel_hashmap/phmap.h>

#include <dwarfs/internal/synchronized.h>

namespace dwarfs::internal {

struct event_tracer_config {
  std::size_t skip_frames = 0;
  std::size_t max_depth = 20;
};

class event_tracer {
 public:
  using fingerprint_type = std::size_t;
  using count_type = std::uint64_t;
  using capture_trace_fn = std::function<cpptrace::raw_trace(
      std::size_t skip_frames, std::size_t max_depth)>;

  explicit event_tracer(event_tracer_config const& config = {});
  event_tracer(event_tracer_config const& config,
               capture_trace_fn capture_trace);

  void trace(std::string_view event_name) const;
  void dump(std::ostream& os, bool color = true) const;

 private:
  struct trace_entry {
    count_type count = 0;
    cpptrace::raw_trace trace;
  };

  using trace_bucket = std::vector<trace_entry>;

  struct event_entry {
    count_type total_count = 0;
    std::unordered_map<fingerprint_type, trace_bucket> traces_by_fingerprint;
  };

  using event_map = phmap::flat_hash_map<std::string, event_entry>;

  static void dump_impl(event_map const& events, std::ostream& os, bool color);
  static fingerprint_type make_fingerprint(cpptrace::raw_trace const& trace);
  static bool
  same_trace(cpptrace::raw_trace const& lhs, cpptrace::raw_trace const& rhs);

  event_tracer_config const config_;
  capture_trace_fn capture_trace_;
  synchronized<event_map> mutable events_;
};

} // namespace dwarfs::internal

#endif // DWARFS_STACKTRACE_ENABLED
