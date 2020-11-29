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
#include <iterator>
#include <locale>
#include <stdexcept>

#include <cxxabi.h>
#include <execinfo.h>

#include <boost/date_time/posix_time/posix_time.hpp>

#include <fmt/format.h>

#include "dwarfs/logger.h"

namespace dwarfs {

namespace {

void backtrace(std::ostream& os) {
  const int stack_size = 5;
  const int stack_offset = 2;

  void* array[stack_size + stack_offset];
  size_t size;
  char** strings;
  size_t i;

  size = ::backtrace(array, stack_size + stack_offset);
  strings = ::backtrace_symbols(array, size);

  for (i = stack_offset; i < size; i++) {
    std::string frame(strings[i]);
    size_t pos = frame.find_last_of('(');
    if (pos != std::string::npos) {
      ++pos;
      size_t end = frame.find_first_of("+)", pos);
      if (end != std::string::npos) {
        std::string sym = std::string(begin(frame) + pos, begin(frame) + end);
        int status;
        char* realsym = ::abi::__cxa_demangle(sym.c_str(), 0, 0, &status);
        frame.replace(pos, end - pos, realsym);
        free(realsym);

        os << "  <" << i - stack_offset << "> " << frame << "\n";
      }
    }
  }

  ::free(strings);
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
  throw std::runtime_error(fmt::format("invalid logger level: {}", level));
}

stream_logger::stream_logger(std::ostream& os, level_type threshold)
    : os_(os) {
  os_.imbue(std::locale(os_.getloc(),
                        new boost::posix_time::time_facet("%H:%M:%S.%f")));
  set_threshold(threshold);
}

void stream_logger::write(level_type level, const std::string& output) {
  if (level <= threshold_) {
    auto t = boost::posix_time::microsec_clock::local_time();

    std::lock_guard<std::mutex> lock(mx_);
    os_ << t << " " << output << "\n";

    if (threshold_ == TRACE) {
      backtrace(os_);
    }
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
} // namespace dwarfs
