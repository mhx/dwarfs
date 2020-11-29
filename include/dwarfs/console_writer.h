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
#include <mutex>
#include <string>

#include "dwarfs/logger.h"

namespace dwarfs {

class progress;

class console_writer : public logger {
 public:
  enum display_mode { NORMAL, REWRITE };

  console_writer(std::ostream& os, bool is_terminal, size_t width,
                 level_type threshold, display_mode mode = NORMAL);

  void write(level_type level, const std::string& output) override;

  void update(const progress& p, bool last);

 private:
  void rewind();

  std::ostream& os_;
  std::mutex mx_;
  std::atomic<level_type> threshold_;
  std::string statebuf_;
  double frac_;
  std::atomic<size_t> counter_{0};
  const bool show_progress_;
  const size_t width_;
  const display_mode mode_;
};
} // namespace dwarfs
