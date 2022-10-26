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
#include <locale>
#include <stdexcept>

#include <folly/Conv.h>

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
    , with_context_(with_context) {
  set_threshold(threshold);
}

void stream_logger::write(level_type level, const std::string& output,
                          char const* file, int line) {
  if (level <= threshold_) {
    auto t = get_current_time_string();
    const char* prefix = "";
    const char* suffix = "";

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
    folly::symbolizer::StringSymbolizePrinter printer(
        color_ ? folly::symbolizer::SymbolizePrinter::COLOR : 0);

    if (threshold_ == TRACE) {
      using namespace folly::symbolizer;
      Symbolizer symbolizer(LocationInfoMode::FULL);
      FrameArray<5> addresses;
      getStackTraceSafe(addresses);
      symbolizer.symbolize(addresses);
      printer.println(addresses, 0);
    }
#endif

    char lchar = logger::level_char(level);
    std::string context;

    if (with_context_ && file) {
      context = get_logger_context(file, line);
      if (color_) {
        context = folly::to<std::string>(
            suffix, terminal_color(termcolor::MAGENTA), context,
            terminal_color(termcolor::NORMAL), prefix);
      }
    }

    std::lock_guard lock(mx_);
    os_ << prefix << lchar << ' ' << t << ' ' << context << output << suffix
        << "\n";

#if DWARFS_SYMBOLIZE
    if (threshold_ == TRACE) {
      os_ << printer.str();
    }
#endif
  }
}

void stream_logger::set_threshold(level_type threshold) {
  threshold_ = threshold;

  if (threshold > level_type::INFO) {
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
