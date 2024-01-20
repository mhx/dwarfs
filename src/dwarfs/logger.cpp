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

#include <cstring>
#include <exception>
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
#include "dwarfs/util.h"

namespace dwarfs {

namespace {

constexpr std::array<std::pair<std::string_view, logger::level_type>, 6>
    log_level_map = {{
        {"error", logger::ERROR},
        {"warn", logger::WARN},
        {"info", logger::INFO},
        {"verbose", logger::VERBOSE},
        {"debug", logger::DEBUG},
        {"trace", logger::TRACE},
    }};

}

std::ostream& operator<<(std::ostream& os, logger::level_type const& optval) {
  return os << logger::level_name(optval);
}

std::istream& operator>>(std::istream& is, logger::level_type& optval) {
  std::string s;
  is >> s;
  optval = logger::parse_level(s);
  return is;
}

logger::level_type logger::parse_level(std::string_view level) {
  // don't parse FATAL here, it's a special case
  for (auto const& [name, lvl] : log_level_map) {
    if (level == name) {
      return lvl;
    }
  }
  DWARFS_THROW(runtime_error, fmt::format("invalid logger level: {}", level));
}

std::string_view logger::level_name(level_type level) {
  for (auto const& [name, lvl] : log_level_map) {
    if (level == lvl) {
      return name;
    }
  }
  DWARFS_THROW(runtime_error, fmt::format("invalid logger level: {}",
                                          static_cast<int>(level)));
}

std::string logger::all_level_names() {
  std::string result;
  for (auto const& m : log_level_map) {
    if (!result.empty()) {
      result += ", ";
    }
    result += m.first;
  }
  return result;
}

stream_logger::stream_logger(std::shared_ptr<terminal const> term,
                             std::ostream& os, logger_options const& logopts)
    : os_(os)
    , color_(term->is_tty(os) && term->is_fancy())
    , enable_stack_trace_{getenv_is_enabled("DWARFS_LOGGER_STACK_TRACE")}
    , with_context_(logopts.with_context ? logopts.with_context.value()
                                         : logopts.threshold >= logger::VERBOSE)
    , term_{std::move(term)} {
  set_threshold(logopts.threshold);
}

void stream_logger::preamble(std::ostream&) {}
void stream_logger::postamble(std::ostream&) {}
std::string_view stream_logger::get_newline() const { return "\n"; }

void stream_logger::write_nolock(std::string_view output) {
  if (&os_ == &std::cerr) {
    fmt::print(stderr, "{}", output);
  } else {
    os_ << output;
  }
}

void stream_logger::write(level_type level, const std::string& output,
                          char const* file, int line) {
  if (level <= threshold_ || level == FATAL) {
    auto t = get_current_time_string();
    std::string_view prefix;
    std::string_view suffix;
    auto newline = get_newline();

    if (color_) {
      switch (level) {
      case FATAL:
      case ERROR:
        prefix = term_->color(termcolor::BOLD_RED);
        suffix = term_->color(termcolor::NORMAL);
        break;

      case WARN:
        prefix = term_->color(termcolor::BOLD_YELLOW);
        suffix = term_->color(termcolor::NORMAL);
        break;

      case VERBOSE:
        prefix = term_->color(termcolor::DIM_CYAN);
        suffix = term_->color(termcolor::NORMAL);
        break;

      case DEBUG:
        prefix = term_->color(termcolor::DIM_YELLOW);
        suffix = term_->color(termcolor::NORMAL);
        break;

      case TRACE:
        prefix = term_->color(termcolor::GRAY);
        suffix = term_->color(termcolor::NORMAL);
        break;

      default:
        break;
      }
    }

#if DWARFS_SYMBOLIZE
    std::string stacktrace;
    std::vector<std::string_view> st_lines;

    if (enable_stack_trace_ || level == FATAL) {
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
            suffix, term_->color(termcolor::DIM_MAGENTA), context,
            term_->color(termcolor::NORMAL), prefix);
      }
    }

    std::string tmp;
    folly::small_vector<std::string_view, 2> lines;

    if (output.find('\r') != std::string::npos) {
      tmp.reserve(output.size());
      std::copy_if(output.begin(), output.end(), std::back_inserter(tmp),
                   [](char c) { return c != '\r'; });
      folly::split('\n', tmp, lines);
    } else {
      folly::split('\n', output, lines);
    }

    if (lines.back().empty()) {
      lines.pop_back();
    }

    std::ostringstream oss;
    bool clear_ctx = true;

    for (auto l : lines) {
      oss << prefix << lchar << ' ' << t << ' ' << context << l << suffix
          << newline;

      if (clear_ctx) {
        std::fill(t.begin(), t.end(), '.');
        context.assign(context_len, ' ');
        clear_ctx = false;
      }
    }

#if DWARFS_SYMBOLIZE
    for (auto l : st_lines) {
      oss << l << newline;
    }
#endif

    std::lock_guard lock(mx_);

    std::ostringstream oss2;

    preamble(oss2);
    oss2 << oss.str();
    postamble(oss2);

    write_nolock(oss2.str());
  }

  if (level == FATAL) {
    std::abort();
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
  return fmt::format("[{0}:{1}] ", basename(path), line);
}

std::string get_current_time_string() {
  using namespace std::chrono;
  auto now = floor<microseconds>(system_clock::now());
  return fmt::format("{:%H:%M:%S}", now);
}

} // namespace dwarfs
