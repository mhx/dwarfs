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

#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <iosfwd>
#include <mutex>
#include <string>

#include "dwarfs/logger.h"

namespace dwarfs {

class progress;

class console_writer : public stream_logger {
 public:
  using get_term_width_type = std::function<size_t()>;

  enum display_mode { NORMAL, REWRITE };
  enum progress_mode { NONE, SIMPLE, ASCII, UNICODE };

  console_writer(std::ostream& os, progress_mode pg_mode,
                 get_term_width_type get_term_width, level_type threshold,
                 display_mode mode = NORMAL, bool verbose = false);

  void update(const progress& p, bool last);

 private:
  void preamble() override;
  void postamble() override;
  std::string_view get_newline() const override;
  void rewind(int next_rewind_lines);

  std::string statebuf_;
  int rewind_lines_{0};
  double frac_;
  std::atomic<size_t> counter_{0};
  progress_mode const pg_mode_;
  get_term_width_type get_term_width_;
  display_mode const mode_;
};
} // namespace dwarfs
