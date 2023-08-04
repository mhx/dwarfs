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

#include <cstdlib>
#include <cstring>
#include <iterator>
#include <stdexcept>

#include <folly/Conv.h>
#include <folly/String.h>
#include <folly/small_vector.h>

#ifndef NDEBUG
#include <folly/experimental/symbolizer/Symbolizer.h>
#define DWARFS_SYMBOLIZE (FOLLY_HAVE_ELF && FOLLY_HAVE_DWARF)
#else
#define DWARFS_SYMBOLIZE 0
#endif

#include <fmt/chrono.h>
#include <fmt/format.h>

#include "dwarfs/logger.h"
#include "dwarfs/terminal.h"

namespace dwarfs {

namespace {

bool get_enable_stack_trace() {
  if (auto var = std::getenv("DWARFS_LOGGER_STACK_TRACE")) {
    if (auto val = folly::tryTo<bool>(var); val && *val) {
      return true;
    }
  }
  return false;
}

} // namespace

logger::level_type logger::parse_level(std::string_view level) {
  if (level == "error") {
    return ERROR;
  }
  if (level == "warn") {
    return WARN;
  }
  if (level == "info") {
    return INFO;
  }
  if (level == "debug") {
    return DEBUG;
  }
  if (level == "trace") {
    return TRACE;
  }
  DWARFS_THROW(runtime_error, fmt::format("invalid logger level: {}", level));
}

stream_logger::stream_logger(std::ostream& os, level_type threshold,
                             bool with_context)
    : os_(os)
    , color_(stream_is_fancy_terminal(os))
    , enable_stack_trace_{get_enable_stack_trace()}
    , with_context_(with_context) {
  set_threshold(threshold);
}

void stream_logger::preamble() {}
void stream_logger::postamble() {}
std::string_view stream_logger::get_newline() const { return "\n"; }

void stream_logger::write(level_type level, const std::string& output,
                          char const* file, int line) {
  if (level <= threshold_) {
    auto t = get_current_time_string();
    const char* prefix = "";
    const char* suffix = "";
    auto newline = get_newline();

    if (color_) {
      switch (level) {
      case ERROR:
        prefix = terminal_color(termcolor::BOLD_RED);
        suffix = terminal_color(termcolor::NORMAL);
        break;

      case WARN:
        prefix = terminal_color(termcolor::BOLD_YELLOW);
        suffix = terminal_color(termcolor::NORMAL);
        break;

      default:
        break;
      }
    }

#if DWARFS_SYMBOLIZE
    std::string stacktrace;
    std::vector<std::string_view> st_lines;

    if (enable_stack_trace_) {
      using namespace folly::symbolizer;
      Symbolizer symbolizer(LocationInfoMode::FULL);
      FrameArray<8> addresses;
      getStackTraceSafe(addresses);
      symbolizer.symbolize(addresses);
      folly::symbolizer::StringSymbolizePrinter printer(
          color_ ? folly::symbolizer::SymbolizePrinter::COLOR : 0);
      printer.println(addresses, 3);
      stacktrace = printer.str();
      folly::split('\n', stacktrace, st_lines);
      if (st_lines.back().empty()) {
        st_lines.pop_back();
      }
    }
#endif

    char lchar = logger::level_char(level);
    std::string context;
    size_t context_len = 0;

    if (with_context_ && file) {
      context = get_logger_context(file, line);
      context_len = context.size();
      if (color_) {
        context = folly::to<std::string>(
            suffix, terminal_color(termcolor::MAGENTA), context,
            terminal_color(termcolor::NORMAL), prefix);
      }
    }

    folly::small_vector<std::string_view, 2> lines;
    folly::split('\n', output, lines);

    if (lines.back().empty()) {
      lines.pop_back();
    }

    bool clear_ctx = true;

    std::lock_guard lock(mx_);

    preamble();

    for (auto l : lines) {
      os_ << prefix << lchar << ' ' << t << ' ' << context << l << suffix
          << newline;

      if (clear_ctx) {
        std::fill(t.begin(), t.end(), '.');
        context.assign(context_len, ' ');
        clear_ctx = false;
      }
    }

#if DWARFS_SYMBOLIZE
    for (auto l : st_lines) {
      os_ << l << newline;
    }
#endif

    postamble();
  }
}

void stream_logger::set_threshold(level_type threshold) {
  threshold_ = threshold;

  if (threshold >= level_type::DEBUG) {
    set_policy<debug_logger_policy>();
  } else {
    set_policy<prod_logger_policy>();
  }
}

std::string get_logger_context(char const* path, int line) {
  auto base = ::strrchr(path, '/');
  return fmt::format("[{0}:{1}] ", base ? base + 1 : path, line);
}

std::string get_current_time_string() {
  using namespace std::chrono;
  auto now = floor<microseconds>(system_clock::now());
  return fmt::format("{:%H:%M:%S}", now);
}

} // namespace dwarfs
