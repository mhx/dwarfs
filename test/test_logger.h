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
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <cstdlib>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <dwarfs/logger.h>
#include <dwarfs/util.h>

namespace dwarfs::test {

class test_logger : public ::dwarfs::logger {
 public:
  struct log_entry {
    log_entry(level_type level, std::string_view output, source_location loc)
        : level{level}
        , output{output}
        , loc{loc} {}

    level_type level;
    std::string output;
    source_location loc;
  };

  test_logger(std::optional<level_type> threshold = std::nullopt)
      : threshold_{threshold ? *threshold : default_threshold()}
      , output_threshold_{output_threshold(default_threshold())}
      , output_{::dwarfs::getenv_is_enabled("DWARFS_TEST_LOGGER_OUTPUT")} {
    if (threshold_ >= level_type::DEBUG ||
        (output_ && output_threshold_ >= level_type::DEBUG)) {
      set_policy<debug_logger_policy>();
    } else {
      set_policy<prod_logger_policy>();
    }
  }

  level_type threshold() const override { return threshold_; }

  void write(level_type level, std::string_view output,
             source_location loc) override {
    if (output_ && level <= output_threshold_) {
      auto const t = get_current_time_string();
      std::lock_guard lock(mx_);
      std::cerr << level_char(level) << ' ' << t << " [" << loc.file_name()
                << ":" << loc.line() << "] " << output << "\n";
    }

    if (level <= threshold_) {
      std::lock_guard lock(mx_);
      log_.emplace_back(level, output, loc);
    }
  }

  std::vector<log_entry> const& get_log() const { return log_; }

  std::string as_string() const {
    std::ostringstream oss;
    for (auto const& entry : log_) {
      oss << level_char(entry.level) << " [" << entry.loc.file_name() << ":"
          << entry.loc.line() << "] " << entry.output << "\n";
    }
    return oss.str();
  }

  bool empty() const { return log_.empty(); }

  void clear() { log_.clear(); }

  friend std::ostream& operator<<(std::ostream& os, test_logger const& logger) {
    os << logger.as_string();
    return os;
  }

 private:
  static level_type default_threshold() { return level_type::INFO; }

  static level_type output_threshold(level_type default_level) {
    if (auto var = std::getenv("DWARFS_TEST_LOGGER_LEVEL")) {
      return ::dwarfs::logger::parse_level(var);
    }
    return default_level;
  }

  std::mutex mx_;
  std::vector<log_entry> log_;
  level_type const threshold_;
  level_type const output_threshold_;
  bool const output_;
};

} // namespace dwarfs::test
