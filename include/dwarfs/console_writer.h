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
#include <iosfwd>
#include <locale>
#include <mutex>
#include <string>

#include "dwarfs/logger.h"

namespace dwarfs {

class progress;

class console_writer : public logger {
 public:
  enum display_mode { NORMAL, REWRITE };
  enum progress_mode { NONE, SIMPLE, ASCII, UNICODE };

  console_writer(std::ostream& os, progress_mode pg_mode, size_t width,
                 level_type threshold, display_mode mode = NORMAL,
                 bool verbose = false);

  void write(level_type level, const std::string& output, char const* file,
             int line) override;

  void update(const progress& p, bool last);

  std::locale const& locale() const override { return locale_; }

 private:
  void rewind();

  std::ostream& os_;
  std::locale locale_;
  std::mutex mx_;
  std::atomic<level_type> threshold_;
  std::string statebuf_;
  double frac_;
  std::atomic<size_t> counter_{0};
  progress_mode const pg_mode_;
  size_t const width_;
  display_mode const mode_;
  bool const color_;
  bool const with_context_;
  bool const debug_progress_;
};
} // namespace dwarfs
